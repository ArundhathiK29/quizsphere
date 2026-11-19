#pragma once

#include <memory>
#include <string>

namespace quizsphere {
struct WebRequest {
    std::string method;
    std::string path;
    std::string body;
    std::string cookie;
    std::string csrf;
};
struct WebResponse {
    int status{200};
    std::string body;
    std::string cookie;
};

// Local browser presentation adapter. Business rules remain in the existing modules.
class WebApplication {
public:
    explicit WebApplication(const std::string& database_path);
    ~WebApplication();
    WebResponse handle(const WebRequest& request);
    void tick();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace quizsphere
