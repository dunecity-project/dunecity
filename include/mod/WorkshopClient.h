#ifndef DUNECITY_WORKSHOP_CLIENT_H
#define DUNECITY_WORKSHOP_CLIENT_H
#include <mod/Workshop.h>
#include <memory>

namespace Workshop {
// Nonblocking chunked transfer of exact revisions for online games: a host uploads an
// authored mod before advertising, and a joiner downloads the revision a game pins.
// update() is driven by a menu loop.
class Client {
public:
    enum class Status { Idle, Busy, Succeeded, Failed };
    Client();
    ~Client();
    void publish(const Revision& revision);
    void download(const std::string& hash);
    void update();
    void cancel();
    Status status() const;
    const std::string& message() const;
    const Revision& result() const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
// Modal progress uses the normal SDL loop; cancellation never publishes a partial revision.
bool publishWithProgress(const Revision& revision);
bool downloadWithProgress(const std::string& hash);
}
#endif
