#include "web_application.hpp"

#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <gtest/gtest.h>

namespace quizsphere {
namespace {
std::string field(const std::string& json,const std::string& key) {
    const auto start=json.find("\""+key+"\":\"");
    if(start==std::string::npos)return "";
    const auto first=start+key.size()+4;
    return json.substr(first,json.find('"',first)-first);
}
struct Identity {std::string cookie,csrf;};
class WebApplicationTest : public ::testing::Test {
protected:
    std::filesystem::path path;
    std::unique_ptr<WebApplication> app;
    void SetUp() override {
        path=std::filesystem::temp_directory_path()/("quizsphere-web-"+std::to_string(std::random_device{}())+".sqlite");
        app=std::make_unique<WebApplication>(path.string());
    }
    void TearDown() override {
        app.reset();std::error_code ignored;
        for(const auto* suffix:{"","-journal","-wal","-shm",".auth.log"})std::filesystem::remove(path.string()+suffix,ignored);
    }
    WebResponse call(const std::string& route,const std::string& body,const Identity& who={}) {
        return app->handle({"POST","/api/"+route,body,who.cookie,who.csrf});
    }
    WebResponse read(const std::string& route,const Identity& who={}) {return app->handle({"GET","/api/"+route,"",who.cookie,""});}
    Identity account(const std::string& name,const std::string& role) {
        EXPECT_EQ(call("register","username="+name+"&password=DemoPass123%21&role="+role).status,200);
        auto login=call("login","username="+name+"&password=DemoPass123%21");
        EXPECT_EQ(login.status,200);return {login.cookie,field(login.body,"csrf")};
    }
    std::string quiz(const Identity& teacher) {
        EXPECT_EQ(call("questions/save","id=q1&subject=Math&difficulty=Easy&text=Two%20plus%20two%3F&options=3%0A4&correct=1&available=true",teacher).status,200);
        auto q=call("quizzes/create","title=Checkpoint&minutes=1&count=1&subject=Math&difficulty=Easy&marks=4&penalty=1",teacher);
        EXPECT_EQ(q.status,200);return field(q.body,"id");
    }
};
TEST_F(WebApplicationTest, AuthRequiresRealSessionCsrfAndValidRegistrationRole) {
    EXPECT_EQ(read("quizzes").status,401);
    EXPECT_EQ(call("register","username=admin&password=DemoPass123%21&role=Admin").status,400);
    auto student=account("student","Student");
    EXPECT_NE(student.cookie.find("HttpOnly; SameSite=Strict"),std::string::npos);
    EXPECT_FALSE(student.csrf.empty());
    EXPECT_EQ(read("session",student).status,200);
    EXPECT_EQ(call("logout","",{student.cookie,"wrong"}).status,403);
    EXPECT_EQ(call("logout","",student).status,200);
    EXPECT_EQ(read("session",student).status,401);
}
TEST_F(WebApplicationTest, StudentCannotReadAnswerKeysOrCreateInstructorContent) {
    auto teacher=account("teacher","Instructor"),student=account("student","Student");
    const auto id=quiz(teacher);
    EXPECT_EQ(read("questions",student).status,403);
    EXPECT_EQ(call("quizzes/create","title=Bad&minutes=1&count=1&marks=1&penalty=0",student).status,403);
    auto start=call("attempt/start","quiz="+id+"&mode=graded",student);
    ASSERT_EQ(start.status,200);
    EXPECT_EQ(start.body.find("correct_option"),std::string::npos);
    EXPECT_EQ(start.body.find("original_option_indices"),std::string::npos);
    EXPECT_NE(start.body.find("\"feedback\":null"),std::string::npos);
    EXPECT_EQ(read("results?quiz="+id,teacher).status,403);
}
TEST_F(WebApplicationTest, GradedSubmissionPersistsAndRetakeIsRejectedAfterServerRestart) {
    auto teacher=account("teacher","Instructor"),student=account("student","Student"),other=account("other","Student");
    const auto id=quiz(teacher);
    ASSERT_EQ(call("attempt/start","quiz="+id+"&mode=graded",student).status,200);
    EXPECT_EQ(call("attempt/action","action=answer&index=0",student).status,200);
    EXPECT_EQ(call("attempt/action","action=submit&confirmed=false",student).status,400);
    auto final=call("attempt/action","action=submit&confirmed=true",student);
    EXPECT_EQ(final.status,200);EXPECT_NE(final.body.find("\"completed\":true"),std::string::npos);
    const auto original=read("results?quiz="+id,student);EXPECT_EQ(original.status,200);
    EXPECT_EQ(read("results?quiz="+id,other).status,400);
    EXPECT_EQ(call("attempt/start","quiz="+id+"&mode=graded",student).status,400);
    app.reset();app=std::make_unique<WebApplication>(path.string());
    auto login=call("login","username=student&password=DemoPass123%21");student={login.cookie,field(login.body,"csrf")};
    EXPECT_EQ(read("results?quiz="+id,student).body,original.body);
    EXPECT_EQ(call("attempt/start","quiz="+id+"&mode=graded",student).status,400);
}
TEST_F(WebApplicationTest, PracticeFeedbackDoesNotCreateAStoredReportOrBlockGradedMode) {
    auto teacher=account("teacher","Instructor"),student=account("student","Student");const auto id=quiz(teacher);
    ASSERT_EQ(call("attempt/start","quiz="+id+"&mode=practice",student).status,200);
    auto answer=call("attempt/action","action=answer&index=0",student);
    EXPECT_NE(answer.body.find("\"answer\":\"4\""),std::string::npos);
    EXPECT_EQ(call("attempt/action","action=submit&confirmed=true",student).status,200);
    EXPECT_EQ(read("results?quiz="+id,student).status,400);
    EXPECT_EQ(read("rankings?type=all",student).body,"[]");
    EXPECT_EQ(call("attempt/start","quiz="+id+"&mode=graded",student).status,200);
}
TEST_F(WebApplicationTest, MalformedFieldsAndReferencedQuestionMutationsAreRejected) {
    auto teacher=account("teacher","Instructor");const auto id=quiz(teacher);
    EXPECT_EQ(call("quizzes/create","title=Bad&minutes=1x&count=1&marks=1&penalty=0",teacher).status,400);
    EXPECT_EQ(call("quizzes/create","title=Bad&minutes=1&count=1&marks=nan&penalty=0",teacher).status,400);
    EXPECT_EQ(call("questions/delete","id=q1&confirmed=true",teacher).status,400);
    EXPECT_EQ(call("questions/availability","id=q1&available=false",teacher).status,200);
    EXPECT_EQ(call("questions/save","id=%00",teacher).status,400);
    EXPECT_EQ(call("questions/save","id=q1&id=q2",teacher).status,400);
    EXPECT_EQ(call("questions/save","id=%xx",teacher).status,400);
    EXPECT_EQ(call("questions/save",std::string(65537,'a'),teacher).status,413);
    EXPECT_FALSE(id.empty());
}
}  // namespace
}  // namespace quizsphere
