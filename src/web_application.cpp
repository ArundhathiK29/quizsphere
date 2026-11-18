#include "web_application.hpp"

#include <openssl/rand.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "analytics_module.hpp"
#include "auth.hpp"
#include "quiz_attempt.hpp"
#include "sqlite_user_repository.hpp"

namespace quizsphere {
namespace {
using Fields = std::map<std::string, std::string>;
using Clock = std::chrono::steady_clock;
struct ApiError : std::runtime_error {
    int status;
    ApiError(int code, const std::string& message) : std::runtime_error(message), status(code) {}
};
std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        else out << static_cast<char>(c);
    }
    out << '"';
    return out.str();
}
std::string number(double value) {
    if (!std::isfinite(value)) throw ApiError(500, "Invalid numeric result.");
    std::ostringstream out;
    out << std::setprecision(14) << value;
    return out.str();
}
std::string array(const std::vector<std::string>& values) {
    std::string out = "[";
    for (const auto& value : values) { if (out.size() > 1) out += ','; out += value; }
    return out + ']';
}
template<class Range, class Formatter>
std::vector<std::string> jsonValues(const Range& values, Formatter format) {
    std::vector<std::string> out;
    out.reserve(values.size());
    std::transform(values.begin(), values.end(), std::back_inserter(out), format);
    return out;
}
int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
std::string decode(const std::string& value) {
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (c == '+') c = ' ';
        else if (c == '%') {
            if (i + 2 >= value.size() || hex(value[i+1]) < 0 || hex(value[i+2]) < 0)
                throw ApiError(400, "Malformed form encoding.");
            c = static_cast<char>(hex(value[i+1]) * 16 + hex(value[i+2])); i += 2;
        }
        if (c == '\0') throw ApiError(400, "NUL characters are not allowed.");
        out += c;
    }
    return out;
}
Fields fields(const std::string& body) {
    if (body.size() > 65536) throw ApiError(413, "Request is too large.");
    Fields out;
    std::size_t start = 0;
    while (start < body.size()) {
        const auto end = body.find('&', start);
        const auto part = body.substr(start, end == std::string::npos ? end : end-start);
        const auto equals = part.find('=');
        const auto key = decode(part.substr(0, equals));
        const auto value = equals == std::string::npos ? "" : decode(part.substr(equals+1));
        if (key.empty() || key.size() > 64 || value.size() > 16384 || out.size() >= 32 || out.count(key))
            throw ApiError(400, "Invalid or duplicate form field.");
        out.emplace(key, value);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}
std::string get(const Fields& values, const std::string& key) {
    const auto it = values.find(key);
    return it == values.end() ? "" : it->second;
}
int integer(const std::string& s, int min, int max) {
    int n = 0;
    const auto result = std::from_chars(s.data(), s.data()+s.size(), n);
    if (s.empty() || result.ec != std::errc{} || result.ptr != s.data()+s.size() || n < min || n > max)
        throw ApiError(400, "Invalid integer value.");
    return n;
}
double decimal(const std::string& s) {
    std::size_t used = 0;
    double n = 0;
    try { n = std::stod(s, &used); } catch (...) { throw ApiError(400, "Invalid scoring value."); }
    if (used != s.size() || !std::isfinite(n)) throw ApiError(400, "Invalid scoring value.");
    return n;
}
std::string token() {
    unsigned char bytes[32];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) throw ApiError(500, "Cannot create a secure session.");
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (const auto c : bytes) { out += digits[c >> 4]; out += digits[c & 15]; }
    return out;
}
std::string sessionToken(const std::string& cookies) {
    std::istringstream in(cookies); std::string part;
    while (std::getline(in, part, ';')) {
        const auto first = part.find_first_not_of(' ');
        if (first != std::string::npos && part.compare(first, 11, "qs_session=") == 0)
            return part.substr(first+11);
    }
    return "";
}
void require(const Result& result) {
    if (!result.ok()) throw ApiError(result.code == ErrorCode::NOT_AUTHORIZED ? 403 : 400, result.message);
}
std::string roleName(Role role) {
    return role == Role::Student ? "Student" : role == Role::Instructor ? "Instructor" : "Admin";
}
std::string authMessage(AuthStatus status) {
    switch (status) {
        case AuthStatus::InvalidCredentials: return "Invalid username or password.";
        case AuthStatus::UsernameAlreadyExists: return "Username already exists.";
        case AuthStatus::AccountDisabled: return "Account is disabled.";
        case AuthStatus::AccountLocked: return "Account is temporarily locked. Try again later.";
        case AuthStatus::StorageError: return "Account storage is unavailable.";
        default: return "Check your username, password and selected role.";
    }
}
}  // namespace

struct WebApplication::Impl {
    struct BrowserSession {
        Session actor;
        std::string name, csrf;
        Clock::time_point touched = Clock::now();
        std::unique_ptr<QuizEngine> engine;
        std::unique_ptr<QuizAttemptSession> attempt;
        std::string timer_error;
    };
    SQLiteUserRepository users;
    std::unique_ptr<DataAccessLayer> dal;
    std::unique_ptr<QuestionBank> bank;
    std::unique_ptr<GradingModule> grading;
    std::unique_ptr<AnalyticsModule> analytics;
    std::unique_ptr<IAuthEventLog> log;
    std::map<std::string, std::unique_ptr<BrowserSession>> sessions;

    explicit Impl(const std::string& path) : users(path) {
        if (!users.isOpen()) throw std::runtime_error(users.error());
        std::string error;
        dal = createSqliteDataAccessLayer(path, &error);
        auto repo = createSqliteQuestionRepository(path, &error);
        if (!dal || !repo) throw std::runtime_error(error);
        bank = createQuestionBank(std::move(repo)); require(bank->load());
        grading = createGradingModule(*bank, *dal->quizzes, *dal->attempts);
        analytics = createAnalyticsModule(*bank, *dal->quizzes, *dal->attempts);
        log = createFileAuthEventLog(path + ".auth.log");
    }
    void expire() {
        for (auto it = sessions.begin(); it != sessions.end();) {
            auto& s = *it->second;
            if (s.attempt && s.attempt->attempt().mode == QuizMode::Graded && !s.attempt->attempt().submitted) {
                std::chrono::milliseconds remaining;
                const auto state = s.attempt->remainingTime(&remaining);
                if (state.ok() && remaining.count() == 0) {
                    GradeReport report;
                    const auto result = s.attempt->submitExpired(&report);
                    s.timer_error = (result.ok() || (result.code == ErrorCode::TIME_EXPIRED && report.auto_submitted))
                        ? "" : result.message;
                }
            }
            if (Clock::now() - s.touched > std::chrono::minutes(30)) it = sessions.erase(it);
            else ++it;
        }
    }
    BrowserSession& authenticated(const WebRequest& request) {
        const auto it = sessions.find(sessionToken(request.cookie));
        if (it == sessions.end()) throw ApiError(401, "Please log in.");
        auto& s = *it->second;
        User user; bool found = false;
        if (!users.findUserById(integer(s.actor.user_id, 1, 2147483647), user, found))
            throw ApiError(500, "Account storage is unavailable.");
        if (!found || !user.enabled || user.role != s.actor.role) {
            sessions.erase(it); throw ApiError(401, "Your account changed. Please log in again.");
        }
        if (request.method == "POST" && request.csrf != s.csrf) throw ApiError(403, "Invalid request token.");
        s.touched = Clock::now();
        return s;
    }
    std::string profile(const BrowserSession& s) const {
        return "{\"id\":"+quote(s.actor.user_id)+",\"name\":"+quote(s.name)+",\"role\":"+quote(roleName(s.actor.role))+",\"csrf\":"+quote(s.csrf)+"}";
    }
    std::string reportJson(const GradeReport& report) const {
        std::vector<std::string> rows;
        for (const auto& item : report.itemised_breakdown) {
            PublicQuestion q; const auto found = bank->getQuestionForQuiz(item.question_id, &q);
            const auto option = [&](int i) { return found.ok() && i >= 0 && static_cast<std::size_t>(i) < q.options.size() ? q.options[static_cast<std::size_t>(i)] : "Unavailable"; };
            rows.push_back("{\"question\":"+quote(found.ok()?q.question_text:item.question_id)+",\"selected\":"+quote(item.blank?"Blank":option(item.selected_option))+",\"answer\":"+quote(option(static_cast<int>(item.correct_option)))+",\"status\":"+quote(item.blank?"Blank":item.correct?"Correct":"Incorrect")+",\"marks\":"+number(item.marks_awarded)+"}");
        }
        const auto topics = jsonValues(report.topic_breakdown, [](const TopicScore& topic) {
            return "{\"subject\":"+quote(topic.subject_tag)+",\"correct\":"+std::to_string(topic.correct)+",\"total\":"+std::to_string(topic.total)+",\"marks\":"+number(topic.marks)+"}";
        });
        return "{\"score\":"+number(report.score)+",\"max\":"+number(report.max_score)+",\"auto\":"+(report.auto_submitted?"true":"false")+",\"items\":"+array(rows)+",\"topics\":"+array(topics)+"}";
    }
    std::string attemptJson(const BrowserSession& s) const {
        if (!s.attempt) throw ApiError(404, "Start a quiz first.");
        const auto& a = s.attempt->attempt();
        if (a.submitted) {
            if (a.mode == QuizMode::Practice) return "{\"completed\":true,\"practice\":true}";
            GradeReport report; require(grading->getReport(a.quiz_id, s.actor.user_id, &report));
            return "{\"completed\":true,\"report\":"+reportJson(report)+"}";
        }
        AttemptQuestionView view; require(s.attempt->question(&view));
        const auto options = jsonValues(view.question.options, quote);
        const auto review = s.attempt->review();
        const auto flags = jsonValues(review.flagged_positions, [](std::size_t p) { return std::to_string(p); });
        std::chrono::milliseconds remaining{};
        if (a.mode == QuizMode::Graded) require(s.attempt->remainingTime(&remaining));
        std::string feedback = "null";
        if (a.mode == QuizMode::Practice && view.selected_display_option != kBlankAnswer) {
            PracticeFeedback f; require(s.attempt->feedback(&f));
            feedback = "{\"correct\":"+std::string(f.correct?"true":"false")+",\"answer\":"+quote(f.correct_answer)+"}";
        }
        return "{\"completed\":false,\"practice\":"+std::string(a.mode==QuizMode::Practice?"true":"false")+",\"quiz\":"+quote(a.quiz_id)+",\"position\":"+std::to_string(s.attempt->position())+",\"total\":"+std::to_string(review.total)+",\"answered\":"+std::to_string(review.answered)+",\"flagged\":"+std::string(view.flagged?"true":"false")+",\"flags\":"+array(flags)+",\"remaining\":"+std::to_string(remaining.count())+",\"question\":"+quote(view.question.question_text)+",\"subject\":"+quote(view.question.subject_tag)+",\"options\":"+array(options)+",\"selected\":"+std::to_string(view.selected_display_option)+",\"feedback\":"+feedback+",\"error\":"+quote(s.timer_error)+"}";
    }
    WebResponse handle(const WebRequest& request) {
        expire();
        if (request.method != "GET" && request.method != "POST") throw ApiError(405, "Method not allowed.");
        const auto split = request.path.find('?');
        const auto path = request.path.substr(0, split);
        const auto values = fields(request.method=="POST" ? request.body : split==std::string::npos ? "" : request.path.substr(split+1));
        const bool post = request.method == "POST";
        if (post && (path=="/api/login" || path=="/api/register")) {
            if (path=="/api/register") {
                const auto role = get(values,"role");
                if (role!="Student" && role!="Instructor") throw ApiError(400,"Choose Student or Instructor.");
                const auto result = registerUser(users,get(values,"username"),get(values,"password"),role=="Student"?Role::Student:Role::Instructor);
                if (!result.ok()) throw ApiError(400,authMessage(result.status));
                return {200,"{\"message\":\"Account created. Please log in.\"}",""};
            }
            LoginOptions options; options.log=log.get();
            const auto result=loginUser(users,get(values,"username"),get(values,"password"),options);
            if (!result.ok()) throw ApiError(401,authMessage(result.status));
            if (sessions.size()>=64) throw ApiError(503,"Too many active sessions. Try again later.");
            auto s=std::make_unique<BrowserSession>();
            s->actor=makeSession(result,static_cast<std::int64_t>(std::time(nullptr)));
            s->name=get(values,"username");s->csrf=token();
            s->engine=createQuizEngine(*bank,*dal->quizzes,*dal->attempts,*grading);
            const auto id=token(),body=profile(*s);sessions.emplace(id,std::move(s));
            return {200,body,"qs_session="+id+"; HttpOnly; SameSite=Strict; Path=/; Max-Age=1800"};
        }
        auto& s=authenticated(request);
        if (!post && path=="/api/session") return {200,profile(s),""};
        if (post && path=="/api/logout") { sessions.erase(sessionToken(request.cookie));return {200,"{}","qs_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0"}; }
        if (!post && path=="/api/quizzes") {
            std::vector<Quiz> quizzes;require(s.engine->listQuizzes(&quizzes));
            const auto rows = jsonValues(quizzes, [](const Quiz& q) {
                return "{\"id\":"+quote(q.quiz_id)+",\"title\":"+quote(q.title)+",\"minutes\":"+std::to_string(q.time_limit_minutes)+",\"count\":"+std::to_string(q.question_ids.size())+",\"marks\":"+number(q.scoring.marks_per_question)+"}";
            });
            return {200,array(rows),""};
        }
        if (post && path=="/api/quizzes/create") {
            QuizConfig c;c.title=get(values,"title");c.time_limit_minutes=integer(get(values,"minutes"),1,1440);c.question_count=integer(get(values,"count"),1,10000);
            if(!get(values,"subject").empty())c.subject_filter=get(values,"subject");
            if(!get(values,"difficulty").empty()){c.difficulty_filter=parseDifficulty(get(values,"difficulty"));if(!c.difficulty_filter)throw ApiError(400,"Invalid difficulty.");}
            c.scoring={decimal(get(values,"marks")),decimal(get(values,"penalty"))};Quiz q;require(s.engine->createQuiz(s.actor,c,&q));return {200,"{\"id\":"+quote(q.quiz_id)+"}",""};
        }
        if (path=="/api/questions" || path.rfind("/api/questions/",0)==0) {
            if(s.actor.role==Role::Student)throw ApiError(403,"Only instructors can manage questions.");
            if(!post && path=="/api/questions") {
                std::vector<Question> questions;require(bank->listQuestions(s.actor,&questions,false,std::nullopt,std::nullopt));
                const auto rows = jsonValues(questions, [](const Question& q) {
                    const auto opts = jsonValues(q.options, quote);
                    return "{\"id\":"+quote(q.question_id)+",\"text\":"+quote(q.question_text)+",\"subject\":"+quote(q.subject_tag)+",\"difficulty\":"+quote(difficultyToString(q.difficulty))+",\"options\":"+array(opts)+",\"correct\":"+std::to_string(q.correct_option)+",\"available\":"+(q.is_available?"true":"false")+"}";
                });
                return {200,array(rows),""};
            }
            if(post && path=="/api/questions/save") {
                Question q;q.question_id=get(values,"id");q.question_text=get(values,"text");q.subject_tag=get(values,"subject");auto difficulty=parseDifficulty(get(values,"difficulty"));if(!difficulty)throw ApiError(400,"Invalid difficulty.");q.difficulty=*difficulty;
                q.question_type=QuestionType::MCQ;q.is_available=get(values,"available")=="true";std::istringstream in(get(values,"options"));std::string option;while(std::getline(in,option)){if(!option.empty()&&option.back()=='\r')option.pop_back();q.options.push_back(option);}q.correct_option=static_cast<std::size_t>(integer(get(values,"correct"),0,99));
                require(get(values,"edit")=="true"?bank->updateQuestion(s.actor,q):bank->addQuestion(s.actor,q));return {200,"{}",""};
            }
            if(post && path=="/api/questions/delete"){if(get(values,"confirmed")!="true")throw ApiError(400,"Please confirm deletion.");require(bank->deleteQuestion(s.actor,get(values,"id"),true));return {200,"{}",""};}
            if(post && path=="/api/questions/availability"){require(bank->setAvailability(s.actor,get(values,"id"),get(values,"available")=="true"));return {200,"{}",""};}
        }
        if(post && path=="/api/attempt/start") {
            if(s.actor.role!=Role::Student)throw ApiError(403,"Only students can attempt quizzes.");
            const auto mode=get(values,"mode");if(mode!="practice"&&mode!="graded")throw ApiError(400,"Choose practice or graded mode.");
            auto attempt=std::make_unique<QuizAttemptSession>(s.actor,*s.engine,*bank);require(attempt->start(get(values,"quiz"),mode=="practice"?QuizMode::Practice:QuizMode::Graded));s.attempt=std::move(attempt);s.timer_error.clear();return {200,attemptJson(s),""};
        }
        if(path=="/api/attempt" || path=="/api/attempt/action") {
            if(s.actor.role!=Role::Student)throw ApiError(403,"Only students can attempt quizzes.");
            if(!s.attempt)throw ApiError(404,"Start a quiz first.");
            if(post && path=="/api/attempt/action") {
                const auto action=get(values,"action");
                if(action=="answer")require(s.attempt->select(integer(get(values,"index"),-1,99)));
                else if(action=="jump")require(s.attempt->jump(static_cast<std::size_t>(integer(get(values,"index"),0,9999))));
                else if(action=="flag")require(s.attempt->flag(get(values,"flagged")=="true"));
                else if(action=="submit"){if(get(values,"confirmed")!="true")throw ApiError(400,"Please confirm submission.");GradeReport report;const auto result=s.attempt->submit(true,&report);if(!result.ok()&&!(result.code==ErrorCode::TIME_EXPIRED&&report.auto_submitted))require(result);}
                else throw ApiError(400,"Unknown quiz action.");
            }else if(post || path!="/api/attempt")throw ApiError(405,"Method not allowed.");
            return {200,attemptJson(s),""};
        }
        if(!post && path=="/api/results") {
            if(s.actor.role!=Role::Student)throw ApiError(403,"Log in as a Student to view your result.");
            GradeReport report;require(grading->getReport(get(values,"quiz"),s.actor.user_id,&report));return {200,reportJson(report),""};
        }
        if(!post && path=="/api/rankings") {
            std::vector<LeaderboardEntry> entries;const auto type=get(values,"type");
            if(type=="quiz")require(analytics->leaderboard(get(values,"quiz"),&entries));
            else if(type=="topic")require(analytics->topicLeaderboard(get(values,"subject"),&entries));
            else if(type=="all")require(analytics->allTimeLeaderboard(&entries));else throw ApiError(400,"Choose a ranking type.");
            const auto rows = jsonValues(entries, [&s](const LeaderboardEntry& entry) {
                return "{\"student\":"+quote(entry.student_id)+",\"score\":"+number(entry.score)+",\"rank\":"+std::to_string(entry.rank)+",\"own\":"+(entry.student_id==s.actor.user_id?"true":"false")+"}";
            });
            return {200,array(rows),""};
        }
        throw ApiError(404,"Unknown API route.");
    }
};
WebApplication::WebApplication(const std::string& path) : impl_(std::make_unique<Impl>(path)) {}
WebApplication::~WebApplication() = default;
WebResponse WebApplication::handle(const WebRequest& request) {
    try { return impl_->handle(request); }
    catch(const ApiError& error) { return {error.status,"{\"error\":"+quote(error.what())+"}",""}; }
    catch(const std::exception&) { return {500,"{\"error\":\"The request could not be completed.\"}",""}; }
}
void WebApplication::tick() { impl_->expire(); }
}  // namespace quizsphere
