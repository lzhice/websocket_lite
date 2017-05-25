#include "WS_Lite.h"
#include "Logging.h"

#include <thread>
#include <chrono>
#include <string>
#include <cstring>
#include <iostream>
#include <vector>

using namespace std::chrono_literals;

void wssautobahntest() {
    // auto listener = SL::WS_LITE::WSListener::CreateListener(3001, TEST_CERTIFICATE_PRIVATE_PASSWORD, TEST_CERTIFICATE_PRIVATE_PATH, TEST_CERTIFICATE_PUBLIC_PATH, TEST_DH_PATH);
    SL::WS_LITE::ThreadCount thrdcount(1);
    SL::WS_LITE::PortNumber port(3001);
    auto listener = SL::WS_LITE::WSListener::CreateListener(thrdcount, port);
    listener.set_ReadTimeout(100);
    listener.set_WriteTimeout(100);
    auto lastheard = std::chrono::high_resolution_clock::now();
    listener.onHttpUpgrade([&](const SL::WS_LITE::WSocket& socket) {
        lastheard = std::chrono::high_resolution_clock::now();
        SL_WS_LITE_LOG(SL::WS_LITE::Logging_Levels::INFO_log_level, "listener::onHttpUpgrade");
    });
    listener.onConnection([&](const SL::WS_LITE::WSocket& socket, const std::unordered_map<std::string, std::string>& header) {
        lastheard = std::chrono::high_resolution_clock::now();
        SL_WS_LITE_LOG(SL::WS_LITE::Logging_Levels::INFO_log_level, "listener::onConnection");
    });
    listener.onMessage([&](const SL::WS_LITE::WSocket& socket, const SL::WS_LITE::WSMessage& message) {
        lastheard = std::chrono::high_resolution_clock::now();
        SL::WS_LITE::WSMessage msg;
        msg.Buffer = std::shared_ptr<unsigned char>(new unsigned char[message.len], [](unsigned char* p) { delete[] p; });
        msg.len = message.len;
        msg.code = message.code;
        msg.data = msg.Buffer.get();
        memcpy(msg.data, message.data, message.len);
        listener.send(socket, msg, false);
    });

    listener.startlistening();
    std::string cmd = "wstest -m fuzzingclient -s ";
    cmd += TEST_FUZZING_PATH;
    system(cmd.c_str());
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - lastheard).count() < 2000) {
        std::this_thread::sleep_for(200ms);
    }
    std::cout << "Exiting autobahn test..." << std::endl;
}
void generaltest() {
    std::cout << "Starting General test..." << std::endl;
    //auto listener = SL::WS_LITE::WSListener::CreateListener(3002, TEST_CERTIFICATE_PRIVATE_PASSWORD, TEST_CERTIFICATE_PRIVATE_PATH, TEST_CERTIFICATE_PUBLIC_PATH, TEST_DH_PATH);
    SL::WS_LITE::ThreadCount thrdcount(1);
    SL::WS_LITE::PortNumber port(3002);
    auto listener = SL::WS_LITE::WSListener::CreateListener(thrdcount, port);
    auto lastheard = std::chrono::high_resolution_clock::now();
    listener.onHttpUpgrade([&](const SL::WS_LITE::WSocket& socket) {
        lastheard = std::chrono::high_resolution_clock::now();
        SL_WS_LITE_LOG(SL::WS_LITE::Logging_Levels::INFO_log_level, "listener::onHttpUpgrade");

    });
    listener.onConnection([&](const SL::WS_LITE::WSocket& socket, const std::unordered_map<std::string, std::string>& header) {
        lastheard = std::chrono::high_resolution_clock::now();
        SL_WS_LITE_LOG(SL::WS_LITE::Logging_Levels::INFO_log_level, "listener::onConnection");

    });
    listener.onMessage([&](const SL::WS_LITE::WSocket& socket, const SL::WS_LITE::WSMessage& message) {
        lastheard = std::chrono::high_resolution_clock::now();
        SL::WS_LITE::WSMessage msg;
        msg.Buffer = std::shared_ptr<unsigned char>(new unsigned char[message.len], [](unsigned char* p) { delete[] p; });
        msg.len = message.len;
        msg.code = message.code;
        msg.data = msg.Buffer.get();
        memcpy(msg.data, message.data, message.len);
        listener.send(socket, msg, false);
    });
    listener.onDisconnection([&](const SL::WS_LITE::WSocket& socket, unsigned short code, const std::string& msg) {
        lastheard = std::chrono::high_resolution_clock::now();
        SL_WS_LITE_LOG(SL::WS_LITE::Logging_Levels::INFO_log_level, "listener::onDisconnection");
    });
    listener.startlistening();

    //auto client = SL::WS_LITE::WSClient::CreateClient(TEST_CERTIFICATE_PUBLIC_PATH);
    auto client = SL::WS_LITE::WSClient::CreateClient(thrdcount);
    client.onHttpUpgrade([&](const SL::WS_LITE::WSocket& socket) {
        lastheard = std::chrono::high_resolution_clock::now();
        SL_WS_LITE_LOG(SL::WS_LITE::Logging_Levels::INFO_log_level, "Client::onHttpUpgrade");

    });
    client.onConnection([&](const SL::WS_LITE::WSocket& socket, const std::unordered_map<std::string, std::string>& header) {
        lastheard = std::chrono::high_resolution_clock::now();
        SL_WS_LITE_LOG(SL::WS_LITE::Logging_Levels::INFO_log_level, "Client::onConnection");
    });
    client.onDisconnection([&](const SL::WS_LITE::WSocket& socket, unsigned short code, const std::string& msg) {
        lastheard = std::chrono::high_resolution_clock::now();
        SL_WS_LITE_LOG(SL::WS_LITE::Logging_Levels::INFO_log_level, "client::onDisconnection");
    });
    client.connect("localhost", port);

    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - lastheard).count() < 2000) {
        std::this_thread::sleep_for(200ms);
    }
}
void multithreadtest() {
    std::cout << "Starting Multi threaded test..." << std::endl;
    SL::WS_LITE::ThreadCount thrdcount(4);
    SL::WS_LITE::PortNumber port(3003);
    auto listener = SL::WS_LITE::WSListener::CreateListener(thrdcount, port);
    auto lastheard = std::chrono::high_resolution_clock::now();
    listener.onHttpUpgrade([&](const SL::WS_LITE::WSocket& socket) {
        lastheard = std::chrono::high_resolution_clock::now();
    });
    listener.onConnection([&](const SL::WS_LITE::WSocket& socket, const std::unordered_map<std::string, std::string>& header) {
        lastheard = std::chrono::high_resolution_clock::now();
    });
    listener.onDisconnection([&](const SL::WS_LITE::WSocket& socket, unsigned short code, const std::string& msg) {
        lastheard = std::chrono::high_resolution_clock::now();
    });    
    listener.onMessage([&](const SL::WS_LITE::WSocket& socket, const SL::WS_LITE::WSMessage& message) {
        lastheard = std::chrono::high_resolution_clock::now();
        SL::WS_LITE::WSMessage msg;
        msg.Buffer = std::shared_ptr<unsigned char>(new unsigned char[message.len], [](unsigned char* p) { delete[] p; });
        msg.len = message.len;
        msg.code = message.code;
        msg.data = msg.Buffer.get();
        memcpy(msg.data, message.data, message.len);
        listener.send(socket, msg, false);
    });
    listener.startlistening();
    std::vector<SL::WS_LITE::WSClient> clients;
    clients.reserve(50);
    for (auto i = 0; i < 50; i++) {
        clients.push_back(SL::WS_LITE::WSClient::CreateClient(thrdcount));
        clients[i].onHttpUpgrade([&](const SL::WS_LITE::WSocket& socket) {
            lastheard = std::chrono::high_resolution_clock::now();
        });
        clients[i].onConnection([&clients, &lastheard, i](const SL::WS_LITE::WSocket& socket, const std::unordered_map<std::string, std::string>& header) {
            lastheard = std::chrono::high_resolution_clock::now();
            SL::WS_LITE::WSMessage msg;
            std::string txtmsg = "testing msg";
            txtmsg += std::to_string(i);
            msg.Buffer = std::shared_ptr<unsigned char>(new unsigned char[txtmsg.size()], [](unsigned char* p) { delete[] p; });
            msg.len = txtmsg.size();
            msg.code = SL::WS_LITE::OpCode::TEXT;
            msg.data = msg.Buffer.get();
            memcpy(msg.data, txtmsg.data(), txtmsg.size());
            clients[i].send(socket, msg, false);
        });
        clients[i].onDisconnection([&](const SL::WS_LITE::WSocket& socket, unsigned short code, const std::string& msg) {
            lastheard = std::chrono::high_resolution_clock::now();
        });
        clients[i].onMessage([&](const SL::WS_LITE::WSocket& socket, const SL::WS_LITE::WSMessage& message) {
            lastheard = std::chrono::high_resolution_clock::now();
        });
        clients[i].connect("localhost", port);
     
    }

    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - lastheard).count() < 2000) {
        std::this_thread::sleep_for(200ms);
    }
}
int main(int argc, char* argv[]) {
    wssautobahntest();
    std::this_thread::sleep_for(1s);
    generaltest();
    std::this_thread::sleep_for(1s);
    multithreadtest();
    return 0;
}
