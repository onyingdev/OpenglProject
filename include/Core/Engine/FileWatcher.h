#pragma once
#include <string>
#include <filesystem>

// Tiny modification-time watcher. Construct with a path, then call changed() each frame:
// it returns true the first time it observes a newer mtime than the last check (seeded at
// construction, so the first call is a no-op). Used to hot-reload config files.
class FileWatcher {
public:
    explicit FileWatcher(std::string path) : m_path(std::move(path)) { m_have = stamp(m_stamp); }

    bool changed() {
        std::filesystem::file_time_type now;
        if (!stamp(now)) return false;                 // missing/unreadable right now
        if (m_have && now == m_stamp) return false;    // unchanged
        m_stamp = now;
        m_have  = true;
        return true;
    }

private:
    bool stamp(std::filesystem::file_time_type& out) const {
        std::error_code ec;
        out = std::filesystem::last_write_time(m_path, ec);
        return !ec;
    }

    std::string                     m_path;
    std::filesystem::file_time_type m_stamp{};
    bool                            m_have = false;
};
