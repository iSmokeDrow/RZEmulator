#pragma once
/*
 *  Copyright (C) 2017-2019 NGemity <https://ngemity.org/>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 3 of the License, or (at your
 *  option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 * Partial implementation taken from glandu2 at https://github.com/glandu2/librzu
 * 
*/
#include "Common.h"
#include <string>
#include <mutex>
#include "NetworkThread.h"
#include "MonitorSession.h"

namespace NGemity
{

struct Server
{
    std::string szName;
    std::string szIPAddress;
    int32_t nPlayerCount;
    uint16_t nPort;
    bool bRequesterEnabled;
};

struct ServerRegion
{
    std::string szRegionName;
    std::string szAuthIPAddress;
    std::vector<Server> vServerList;
};

class ServerMonitor
{
  public:
    static ServerMonitor &Instance()
    {
        static ServerMonitor instance;
        return instance;
    }

    void InitializeServerMonitor();
    void InitializeMonitoring(std::shared_ptr<NGemity::Asio::IoContext> pIoContext);
    void Update();

    std::string GetEverything();

    const std::vector<ServerRegion> &GetServerList() const { return m_vServerRegion; }

  private:
    std::vector<ServerRegion> m_vServerRegion{};
    std::shared_ptr<NGemity::Asio::IoContext> _ioContext;
    std::shared_ptr<boost::asio::deadline_timer> _updateTimer;
    bool _stopped;

  protected:
    ServerMonitor();
    std::mutex m_pMutex;
};
} // namespace NGemity
#define sServerMonitor NGemity::ServerMonitor::Instance()