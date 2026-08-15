// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2026 MicroCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "db.h"
#include "net.h"
#include "main.h"
#include "addrman.h"
#include "ui_interface.h"
#include "util.h"

#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <vector>
#include <string>

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#endif

using namespace std;
using namespace boost;

static const int MAX_OUTBOUND_CONNECTIONS = 32;
static const int MICROCOIN_DEFAULT_PORT   = 29841;

// Global state
bool fDiscover = true;
uint64_t nLocalServices = NODE_NETWORK;

static CCriticalSection cs_mapLocalHost;
static map<CNetAddr, int> mapLocalHostScore;
static map<CNetAddr, int> mapLocalHostPort;

static bool vfReachable[NET_MAX] = {};
static bool vfLimited[NET_MAX]   = {};

static CNode* pnodeSync = NULL;
static std::vector<SOCKET> vhListenSocket;

CAddrMan addrman;

vector<CNode*> vNodes;
CCriticalSection cs_vNodes;

static CSemaphore* semOutbound = NULL;

vector<std::string> vAddedNodes;
CCriticalSection cs_vAddedNodes;

vector<std::string> vHttpSeeds; // MicroCoin HTTP seed list
vHttpSeeds.push_back("http://microcoin.rf.gd/dnsseed");
// -----------------------------------------------------------------------------
// Utility: basic HTTP GET (blocking, minimal)
// -----------------------------------------------------------------------------

static bool HttpGet(const std::string& url, std::string& out)
{
    // Expected format: http://host[:port]/path
    if (!boost::starts_with(url, "http://"))
        return false;

    std::string rest = url.substr(7);
    std::string host, path = "/";
    std::string port = "80";

    size_t slashPos = rest.find('/');
    if (slashPos != std::string::npos) {
        host = rest.substr(0, slashPos);
        path = rest.substr(slashPos);
    } else {
        host = rest;
    }

    size_t colonPos = host.find(':');
    if (colonPos != std::string::npos) {
        port = host.substr(colonPos + 1);
        host = host.substr(0, colonPos);
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family   = AF_UNSPEC;

    struct addrinfo* res = NULL;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res)
        return false;

    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) {
        freeaddrinfo(res);
        return false;
    }

    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
#ifdef WIN32
        closesocket(s);
#else
        close(s);
#endif
        freeaddrinfo(res);
        return false;
    }

    freeaddrinfo(res);

    std::string req = "GET " + path + " HTTP/1.0\r\nHost: " + host + "\r\nConnection: close\r\n\r\n";
    send(s, req.c_str(), req.size(), 0);

    char buf[4096];
    std::string resp;
    int n;
    while ((n = recv(s, buf, sizeof(buf), 0)) > 0) {
        resp.append(buf, n);
    }

#ifdef WIN32
    closesocket(s);
#else
    close(s);
#endif

    // Strip headers
    size_t headerEnd = resp.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        return false;

    out = resp.substr(headerEnd + 4);
    return true;
}

// -----------------------------------------------------------------------------
// MicroCoin HTTP seed handling
// Format: each line is either IPv4 or IPv6, optionally with :port
// -----------------------------------------------------------------------------

static void ParseHttpSeedList(const std::string& data, std::vector<CService>& out)
{
    std::vector<std::string> lines;
    boost::split(lines, data, boost::is_any_of("\r\n"));

    for (auto& line : lines) {
        boost::trim(line);
        if (line.empty())
            continue;

        std::string host = line;
        int port = MICROCOIN_DEFAULT_PORT;

        size_t colonPos = std::string::npos;

        // IPv6 with port: [::1]:29841
        if (!line.empty() && line[0] == '[') {
            size_t endBracket = line.find(']');
            if (endBracket != std::string::npos) {
                host = line.substr(1, endBracket - 1);
                colonPos = line.find(':', endBracket);
                if (colonPos != std::string::npos) {
                    port = atoi(line.substr(colonPos + 1).c_str());
                }
            }
        } else {
            colonPos = line.rfind(':');
            if (colonPos != std::string::npos && line.find(':') == colonPos) {
                // Single colon, treat as host:port
                host = line.substr(0, colonPos);
                port = atoi(line.substr(colonPos + 1).c_str());
            }
        }

        CService serv(host, port);
        if (serv.IsValid())
            out.push_back(serv);
    }
}

void FetchHttpSeed(const std::string& url)
{
    std::string data;
    if (!HttpGet(url, data)) {
        LogPrintf("HTTP seed: failed to fetch %s\n", url);
        return;
    }

    std::vector<CService> services;
    ParseHttpSeedList(data, services);

    for (auto& s : services) {
        CAddress addr(s, NODE_NETWORK);
        addr.nTime = GetAdjustedTime() - GetRand(7 * 24 * 60 * 60);
        addrman.Add(addr, CNetAddr());
    }

    LogPrintf("HTTP seed: loaded %u addresses from %s\n", services.size(), url);
}

// -----------------------------------------------------------------------------
// Local address handling
// -----------------------------------------------------------------------------

unsigned short GetListenPort()
{
    return (unsigned short)(GetArg("-port", Params().GetDefaultPort()));
}

bool AddLocal(const CService& addr, int nScore)
{
    if (!addr.IsRoutable())
        return false;

    if (!fDiscover && nScore < LOCAL_MANUAL)
        return false;

    if (IsLimited(addr))
        return false;

    LogPrintf("AddLocal(%s,%i)\n", addr.ToString(), nScore);

    {
        LOCK(cs_mapLocalHost);
        bool fAlready = mapLocalHostScore.count(addr) > 0;
        int& score = mapLocalHostScore[addr];
        int& port  = mapLocalHostPort[addr];
        if (!fAlready || nScore >= score) {
            score = nScore + (fAlready ? 1 : 0);
            port  = addr.GetPort();
        }
        vfReachable[addr.GetNetwork()] = true;
    }

    return true;
}

bool AddLocal(const CNetAddr& addr, int nScore)
{
    return AddLocal(CService(addr, GetListenPort()), nScore);
}

bool IsLimited(enum Network net)
{
    LOCK(cs_mapLocalHost);
    return vfLimited[net];
}

bool IsLimited(const CNetAddr& addr)
{
    return IsLimited(addr.GetNetwork());
}

bool IsLocal(const CService& addr)
{
    LOCK(cs_mapLocalHost);
    return mapLocalHostScore.count(addr) > 0;
}

bool IsReachable(const CNetAddr& addr)
{
    LOCK(cs_mapLocalHost);
    enum Network net = addr.GetNetwork();
    return vfReachable[net] && !vfLimited[net];
}

CAddress GetLocalAddress(const CNetAddr* paddrPeer)
{
    CAddress ret(CService("0.0.0.0", GetListenPort()), 0);
    ret.nServices = nLocalServices;
    ret.nTime     = GetAdjustedTime();
    return ret;
}

// -----------------------------------------------------------------------------
// Node management
// -----------------------------------------------------------------------------

CNode* FindNode(const CService& addr)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
        if ((CService)pnode->addr == addr)
            return pnode;
    return NULL;
}

CNode* ConnectNode(CAddress addrConnect, const char* pszDest)
{
    if (pszDest == NULL) {
        if (IsLocal(addrConnect))
            return NULL;

        CNode* pnode = FindNode((CService)addrConnect);
        if (pnode) {
            pnode->AddRef();
            return pnode;
        }
    }

    LogPrint("net", "trying connection %s lastseen=%.1fhrs\n",
             pszDest ? pszDest : addrConnect.ToString(),
             pszDest ? 0 : (double)(GetAdjustedTime() - addrConnect.nTime) / 3600.0);

    SOCKET hSocket;
    bool proxyConnectionFailed = false;

    if (!ConnectSocket(addrConnect, hSocket, nConnectTimeout, &proxyConnectionFailed)) {
        if (!proxyConnectionFailed)
            addrman.Attempt(addrConnect);
        return NULL;
    }

    addrman.Attempt(addrConnect);
    LogPrint("net", "connected %s\n", pszDest ? pszDest : addrConnect.ToString());

#ifdef WIN32
    u_long nOne = 1;
    ioctlsocket(hSocket, FIONBIO, &nOne);
#else
    fcntl(hSocket, F_SETFL, O_NONBLOCK);
#endif

    CNode* pnode = new CNode(hSocket, addrConnect, pszDest ? pszDest : "", false);
    pnode->AddRef();

    {
        LOCK(cs_vNodes);
        vNodes.push_back(pnode);
    }

    pnode->nTimeConnected = GetTime();
    return pnode;
}

// -----------------------------------------------------------------------------
// Socket handler thread
// -----------------------------------------------------------------------------

static list<CNode*> vNodesDisconnected;

void ThreadSocketHandler()
{
    unsigned int nPrevNodeCount = 0;

    // ---------------------------------------------------------------------
    // MicroCoin: HTTP seed bootstrap
    // ---------------------------------------------------------------------
    {
        LOCK(cs_vAddedNodes);
        for (auto& url : vHttpSeeds) {
            FetchHttpSeed(url);
        }
    }

    while (true)
    {
        {
            LOCK(cs_vNodes);
            vector<CNode*> vNodesCopy = vNodes;
            BOOST_FOREACH(CNode* pnode, vNodesCopy)
            {
                if (pnode->fDisconnect ||
                    (pnode->GetRefCount() <= 0 && pnode->vRecvMsg.empty() &&
                     pnode->nSendSize == 0 && pnode->ssSend.empty()))
                {
                    vNodes.erase(remove(vNodes.begin(), vNodes.end(), pnode), vNodes.end());
                    pnode->CloseSocketDisconnect();
                    if (pnode->fNetworkNode || pnode->fInbound)
                        pnode->Release();
                    vNodesDisconnected.push_back(pnode);
                }
            }
        }

        {
            list<CNode*> vNodesDisconnectedCopy = vNodesDisconnected;
            BOOST_FOREACH(CNode* pnode, vNodesDisconnectedCopy)
            {
                if (pnode->GetRefCount() <= 0)
                {
                    bool fDelete = false;
                    {
                        TRY_LOCK(pnode->cs_vSend, lockSend);
                        if (lockSend)
                        {
                            TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                            if (lockRecv)
                            {
                                TRY_LOCK(pnode->cs_inventory, lockInv);
                                if (lockInv)
                                    fDelete = true;
                            }
                        }
                    }
                    if (fDelete)
                    {
                        vNodesDisconnected.remove(pnode);
                        delete pnode;
                    }
                }
            }
        }

        if (vNodes.size() != nPrevNodeCount) {
            nPrevNodeCount = vNodes.size();
            uiInterface.NotifyNumConnectionsChanged(nPrevNodeCount);
        }

        struct timeval timeout;
        timeout.tv_sec  = 0;
        timeout.tv_usec = 50000;

        fd_set fdsetRecv;
        fd_set fdsetSend;
        fd_set fdsetError;
        FD_ZERO(&fdsetRecv);
        FD_ZERO(&fdsetSend);
        FD_ZERO(&fdsetError);
        SOCKET hSocketMax = 0;
        bool have_fds = false;

        BOOST_FOREACH(SOCKET hListenSocket, vhListenSocket) {
            FD_SET(hListenSocket, &fdsetRecv);
            hSocketMax = max(hSocketMax, hListenSocket);
            have_fds   = true;
        }

        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodes)
            {
                if (pnode->hSocket == INVALID_SOCKET)
                    continue;

                TRY_LOCK(pnode->cs_vSend, lockSend);
                if (!lockSend)
                    continue;

                if (!pnode->vSendMsg.empty())
                    FD_SET(pnode->hSocket, &fdsetSend);
                else
                    FD_SET(pnode->hSocket, &fdsetRecv);

                FD_SET(pnode->hSocket, &fdsetError);
                hSocketMax = max(hSocketMax, pnode->hSocket);
                have_fds   = true;
            }
        }

        int nSelect = select(have_fds ? hSocketMax + 1 : 0,
                             &fdsetRecv, &fdsetSend, &fdsetError, &timeout);

        boost::this_thread::interruption_point();

        if (nSelect == SOCKET_ERROR)
        {
            if (have_fds)
            {
                int nErr = WSAGetLastError();
                LogPrintf("socket select error %d\n", nErr);
            }
            MilliSleep(timeout.tv_usec / 1000);
            continue;
        }

        // Accept new connections
        BOOST_FOREACH(SOCKET hListenSocket, vhListenSocket)
        if (hListenSocket != INVALID_SOCKET && FD_ISSET(hListenSocket, &fdsetRecv))
        {
            struct sockaddr_storage sockaddr;
            socklen_t len = sizeof(sockaddr);
            SOCKET hSocket = accept(hListenSocket, (struct sockaddr*)&sockaddr, &len);
            CAddress addr;

            if (hSocket != INVALID_SOCKET)
                if (!addr.SetSockAddr((const struct sockaddr*)&sockaddr))
                    LogPrintf("Warning: Unknown socket family\n");

            int nInbound = 0;
            {
                LOCK(cs_vNodes);
                BOOST_FOREACH(CNode* pnode, vNodes)
                    if (pnode->fInbound)
                        nInbound++;
            }

            if (hSocket == INVALID_SOCKET)
            {
                int nErr = WSAGetLastError();
                if (nErr != WSAEWOULDBLOCK)
                    LogPrintf("socket error accept failed: %d\n", nErr);
            }
            else if (nInbound >= GetArg("-maxconnections", 125) - MAX_OUTBOUND_CONNECTIONS)
            {
#ifdef WIN32
                closesocket(hSocket);
#else
                close(hSocket);
#endif
            }
            else
            {
                LogPrint("net", "accepted connection %s\n", addr.ToString());
                CNode* pnode = new CNode(hSocket, addr, "", true);
                pnode->AddRef();
                {
                    LOCK(cs_vNodes);
                    vNodes.push_back(pnode);
                }
            }
        }

        // Service sockets
        vector<CNode*> vNodesCopy;
        {
            LOCK(cs_vNodes);
            vNodesCopy = vNodes;
            BOOST_FOREACH(CNode* pnode, vNodesCopy)
                pnode->AddRef();
        }

        BOOST_FOREACH(CNode* pnode, vNodesCopy)
        {
            boost::this_thread::interruption_point();

            if (pnode->hSocket == INVALID_SOCKET)
                continue;

            // Receive
            if (FD_ISSET(pnode->hSocket, &fdsetRecv) || FD_ISSET(pnode->hSocket, &fdsetError))
            {
                TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                if (lockRecv)
                {
                    char pchBuf[0x10000];
                    int nBytes = recv(pnode->hSocket, pchBuf, sizeof(pchBuf), MSG_DONTWAIT);
                    if (nBytes > 0)
                    {
                        if (!pnode->ReceiveMsgBytes(pchBuf, nBytes))
                            pnode->CloseSocketDisconnect();
                        pnode->nLastRecv  = GetTime();
                        pnode->nRecvBytes += nBytes;
                        pnode->RecordBytesRecv(nBytes);
                    }
                    else if (nBytes == 0)
                    {
                        pnode->CloseSocketDisconnect();
                    }
                    else if (nBytes < 0)
                    {
                        int nErr = WSAGetLastError();
                        if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE &&
                            nErr != WSAEINTR && nErr != WSAEINPROGRESS)
                        {
                            pnode->CloseSocketDisconnect();
                        }
                    }
                }
            }

            // Send
            if (FD_ISSET(pnode->hSocket, &fdsetSend))
            {
                TRY_LOCK(pnode->cs_vSend, lockSend);
                if (lockSend)
                    SocketSendData(pnode);
            }

            // Inactivity
            int64_t nTime = GetTime();
            if (nTime - pnode->nTimeConnected > 60)
            {
                if (pnode->nLastRecv == 0 || pnode->nLastSend == 0)
                    pnode->fDisconnect = true;
            }
        }

        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodesCopy)
                pnode->Release();
        }
    }
}
case MSG_MICROBLOCK:
    CMicroBlock mb;
    vRecv >> mb;
    ProcessMicroBlock(mb);
    break;
