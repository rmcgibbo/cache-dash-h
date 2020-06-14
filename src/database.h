#include "utils.h"
#include <SQLiteCpp/SQLiteCpp.h>

#include <ctime>
#include <memory>
#include <stdexcept>
#include <stdlib.h>
#include <string>
#include <vector>

namespace cache_dash_h {

struct Database {
    Database(const std::string& path, bool verbose)
        : db_(SQLite::Database(path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE))
        , verbose_(verbose) {

        try {
            // force it to throw an exception if the database is read-only
            db_.exec("PRAGMA user_version = 0;");
            is_readonly_ = false;
        } catch (const SQLite::Exception& e) {
            if (strcmp(e.what(), "attempt to write a readonly database") == 0) {
                is_readonly_ = true;
            } else {
                throw;
            }
        }

        int num_tables = db_.execAndGet("SELECT COUNT(*) from sqlite_master where type = 'table'");
        schema_created_ = (num_tables > 0);
        if ((!is_readonly_) && (!schema_created_)) {
            InitializeTables();
        }
    }

    void InitializeTables() {
        db_.exec(R"EOF(
        CREATE TABLE cmdline (
            id             INTEGER PRIMARY KEY,
            argv           TEXT        NOT NULL,
            hash           TEXT        NOT NULL,
            ctime          INTEGER     NOT NULL,
            atime          INTEGER     NOT NULL,
            help           TEXT        NOT NULL,
            exit_status    INTEGER     NOT NULL
        );
        CREATE TABLE file (
            id             INTEGER PRIMARY KEY,
            path           TEXT        NOT NULL,
            hash           TEXT        NOT NULL UNIQUE
        );
        CREATE TABLE cmdline_file (
            id             INTEGER PRIMARY KEY,
            cmdline_id     INTEGER,
            file_id        INTEGER,
            FOREIGN KEY (cmdline_id) REFERENCES cmdline (id),
            FOREIGN KEY (file_id) REFERENCES file (id),
            UNIQUE(cmdline_id, file_id)
        );
        )EOF");
        schema_created_ = true;
    }
    void QueryAndPrintHelpAndExitIfPossible(const std::string& cmdhash) {
        if (!schema_created_) {
            return;
        }
        SQLite::Statement q(db_, R"EOF(
        SELECT
            cmdline.help,
            cmdline.exit_status,
            group_concat(file.path, "::::::::::") as path,
            group_concat(file.hash, "::::::::::") as hash,
            cmdline.id
        FROM cmdline
        JOIN cmdline_file ON cmdline.id = cmdline_file.cmdline_id
        JOIN file on cmdline_file.file_id = file.id
        WHERE cmdline.hash = ?
        GROUP BY cmdline.id
        ORDER BY cmdline.id DESC;
        )EOF");
        q.bind(1, cmdhash);

        while (q.executeStep()) {
            std::vector<std::string> paths;
            std::vector<std::string> hashes;

            str::split(q.getColumn("path"), "::::::::::", [&](const std::string s) {
                if (s.size() > 0)
                    paths.push_back(s);
            });
            str::split(q.getColumn("hash"), "::::::::::", [&](const std::string s) {
                if (s.size() > 0) {
                    hashes.push_back(s);
                }
            });
            if (paths.size() != hashes.size()) {
                fprintf(stderr, "paths.size=%zu\n", paths.size());
                fprintf(stderr, "hashes.size=%zu\n", hashes.size());
                for (auto const& path : paths) {
                    fprintf(stderr, "  %s\n", path.c_str());
                }
                fprintf(stderr, "--\n");
                for (auto const& hash : hashes) {
                    fprintf(stderr, "  %s\n", hash.c_str());
                }
                throw std::runtime_error("sizes don't match\n");
            }
            bool match = true;
            size_t i;
            for (i = 0; i < paths.size(); i++) {
                if (hash_filename(paths[i]) != hashes[i]) {
                    // printf("nomatch %s (got=%s) exp=%s\n", paths[i].c_str(),
                    // hash_filename(paths[i]).c_str(),
                    //       hashes[i].c_str());
                    match = false;
                    break;
                }
            }
            if ((i > 0) && match) {
                std::string help = q.getColumn("help");
                int exit_status = q.getColumn("exit_status");
                printf("%s", help.c_str());
                if (verbose_) {
                    printf("%s: Read from cache '%s'\n", program_invocation_short_name,
                           db_.getFilename().c_str());
                }
                if (!is_readonly_) {
                    SQLite::Statement u(db_, "UPDATE cmdline SET atime=? WHERE id=?");
                    u.bind(1, std::time(nullptr));
                    u.bind(2, q.getColumn("id").getText());
                    u.exec();
                }
                exit(exit_status);
            }
        }
        return;
    }

    int Insert(const std::vector<std::string>& cmd,
               const std::string& cmdhash,
               const std::pair<std::string, int>& output,
               const std::vector<std::string>& depfiles) {
        // Begin transaction
        SQLite::Transaction transaction(db_);

        SQLite::Statement insert1(db_, R"EOF(
            INSERT INTO cmdline (id, argv, hash, ctime, atime, help, exit_status)
            VALUES (NULL, ?, ?, ?, ?, ?, ?);
        )EOF");
        auto time = std::time(nullptr);
        insert1.bind(1, str::join(cmd, " "));
        insert1.bind(2, cmdhash);
        insert1.bind(3, time);
        insert1.bind(4, time);
        insert1.bind(5, output.first);
        insert1.bind(6, output.second);

        insert1.exec();
        auto cmdline_id = db_.getLastInsertRowid();

        for (auto const& path : depfiles) {
            auto hash = hash_filename(path);
            SQLite::Statement insert2(db_, R"EOF(
                INSERT OR IGNORE INTO file (id, path, hash)
                VALUES (NULL, ?, ?);
            )EOF");
            insert2.bind(1, path);
            insert2.bind(2, hash);

            int64_t file_id;
            if (insert2.exec() == 0) {
                SQLite::Statement q(db_, "SELECT id from file where hash=?");
                q.bind(1, hash);
                q.executeStep();
                file_id = q.getColumn(0);
            } else {
                file_id = db_.getLastInsertRowid();
            }

            SQLite::Statement ii(
                db_,
                "INSERT OR IGNORE INTO cmdline_file (id, cmdline_id, file_id) VALUES(NULL, ?, ?);");
            ii.bind(1, cmdline_id);
            ii.bind(2, file_id);
            ii.exec();
        }
        transaction.commit();
        return 1;
    }

    SQLite::Database db_;
    bool verbose_;
    bool is_readonly_;
    bool schema_created_;
};

} // namespace cache_dash_h