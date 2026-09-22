/*****************************************************************************
 * parse.cpp: Parser parse task regression test
 *****************************************************************************
 * Copyright © 2026 libvlcpp authors & VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "vlcpp/vlc.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <iostream>
#include <utility>

int main(int ac, char** av)
{
    if (ac < 2)
    {
        std::cerr << "usage: " << av[0] << " <file to parse>" << std::endl;
        return 1;
    }

    const char* vlcArgs = "-vv";
    auto instance = VLC::Instance(1, &vlcArgs);

    /* default config */
    VLC::Parser parser(instance);
    VLC::Media media(av[1], VLC::Media::FromPath);
    VLC::Parser::Request req(media);
    req.setParseFlags(VLC::Parser::ParseFlags::Parse |
                      VLC::Parser::ParseFlags::FetchLocal);

    VLC::Parser::Status parserStatus = VLC::Parser::Status::Failed;
    bool parsingFinished = false;
    std::atomic<int64_t> reportedDuration{-1};
    std::mutex stateMutex;
    std::condition_variable stateCv;
    VLC::Parser::TaskIdentifier expectedId;

    VLC::Parser::Callbacks cbs([&](VLC::Parser::TaskIdentifier task, VLC::Parser::Status status) {
        std::lock_guard<std::mutex> lk(stateMutex);
        assert(task == expectedId);
        reportedDuration.store(media.duration().count());
        parserStatus = status;
        parsingFinished = true;
        stateCv.notify_all();
    });

    /* the task identifier is fetched before submission, so the callback can
       safely compare against it even if it fires before submit() returns */
    auto task = parser.createParseTask(req, cbs);
    expectedId = task.id();
    auto submitted = parser.submit(std::move(task));
    assert(submitted.id() == expectedId);
    assert(submitted.getMedia() == media);

    /* block until the parsing is done */
    {
        std::unique_lock<std::mutex> lk(stateMutex);
        assert(stateCv.wait_for(lk, std::chrono::seconds(5),
                                [&] { return parsingFinished; }));
    }

    assert(parserStatus == VLC::Parser::Status::Done);
    assert(reportedDuration.load() > 0);

    return 0;
}
