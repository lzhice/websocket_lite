#pragma once
#include "DataStructures.h"
#include "Utils.h"

#include <deque>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>

namespace SL {
namespace WS_LITE {
    template <bool isServer, class SOCKETTYPE>
    void ReadHeaderNext(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, const std::shared_ptr<asio::streambuf> &extradata);
    template <bool isServer, class SOCKETTYPE>
    void ReadHeaderStart(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, const std::shared_ptr<asio::streambuf> &extradata);
    template <bool isServer, class SOCKETTYPE, class SENDBUFFERTYPE>
    void write_end(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, const SENDBUFFERTYPE &msg);

    inline size_t ReadFromExtraData(unsigned char *dst, size_t desired_bytes_to_read, const std::shared_ptr<asio::streambuf> &extradata)
    {
        size_t dataconsumed = 0;
        if (extradata->size() >= desired_bytes_to_read) {
            dataconsumed = desired_bytes_to_read;
        }
        else {
            dataconsumed = extradata->size();
        }
        if (dataconsumed > 0) {
            desired_bytes_to_read -= dataconsumed;
            memcpy(dst, asio::buffer_cast<const void *>(extradata->data()), dataconsumed);
            extradata->consume(dataconsumed);
        }
        return dataconsumed;
    }
    template <bool isServer, class SOCKETTYPE>
    void readexpire_from_now(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, std::chrono::seconds secs)
    {
        std::error_code ec;
        if (secs.count() == 0)
            socket->read_deadline.cancel(ec);
        socket->read_deadline.expires_from_now(secs, ec);
        if (ec) {
            SL_WS_LITE_LOG(Logging_Levels::ERROR_log_level, ec.message());
        }
        else if (secs.count() > 0) {
            socket->read_deadline.async_wait([parent, socket](const std::error_code &ec) {
                if (ec != asio::error::operation_aborted) {
                    return sendclosemessage<isServer>(parent, socket, 1001, "read timer expired on the socket ");
                }
            });
        }
    }
    template <bool isServer, class SOCKETTYPE>
    void start_ping(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, std::chrono::seconds secs)
    {
        std::error_code ec;
        if (secs.count() == 0)
            socket->ping_deadline.cancel(ec);
        socket->ping_deadline.expires_from_now(secs, ec);
        if (ec) {
            SL_WS_LITE_LOG(Logging_Levels::ERROR_log_level, ec.message());
        }
        else if (secs.count() > 0) {
            socket->ping_deadline.async_wait([parent, socket, secs](const std::error_code &ec) {
                if (ec != asio::error::operation_aborted) {
                    WSMessage msg;
                    char p[] = "ping";
                    msg.Buffer = std::shared_ptr<unsigned char>(new unsigned char[sizeof(p)], [](unsigned char *ptr) { delete[] ptr; });
                    memcpy(msg.Buffer.get(), p, sizeof(p));
                    msg.len = sizeof(p);
                    msg.code = OpCode::PING;
                    msg.data = msg.Buffer.get();
                    SL::WS_LITE::sendImpl<isServer>(parent, socket, msg, CompressionOptions::NO_COMPRESSION);
                    start_ping<isServer>(parent, socket, secs);
                }
            });
        }
    }
    template <bool isServer, class SOCKETTYPE>
    void writeexpire_from_now(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, std::chrono::seconds secs)
    {

        std::error_code ec;
        if (secs.count() == 0)
            socket->write_deadline.cancel(ec);
        socket->write_deadline.expires_from_now(secs, ec);
        if (ec) {
            SL_WS_LITE_LOG(Logging_Levels::ERROR_log_level, ec.message());
        }
        else if (secs.count() > 0) {
            socket->write_deadline.async_wait([parent, socket](const std::error_code &ec) {
                if (ec != asio::error::operation_aborted) {
                    return sendclosemessage<isServer>(parent, socket, 1001, "write timer expired on the socket ");
                }
            });
        }
    }

    template <bool isServer, class SOCKETTYPE, class SENDBUFFERTYPE>
    void writeend(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, const SENDBUFFERTYPE &msg, bool iserver)
    {
        if (!iserver) {
            std::uniform_int_distribution<unsigned int> dist(0, 255);
            std::random_device rd;

            unsigned char mask[4];
            for (auto c = 0; c < 4; c++) {
                mask[c] = static_cast<unsigned char>(dist(rd));
            }
            auto p = reinterpret_cast<unsigned char *>(msg.data);
            for (decltype(msg.len) i = 0; i < msg.len; i++) {
                *p++ ^= mask[i % 4];
            }
            std::error_code ec;
            auto bytes_transferred = asio::write(socket->Socket, asio::buffer(mask, 4), ec);
            if (ec) {
                if (msg.code == OpCode::CLOSE) {
                    return handleclose(parent, socket, msg.code, "");
                }
                else {
                    return handleclose(parent, socket, 1002, "write mask failed " + ec.message());
                }
            }
            else {
                UNUSED(bytes_transferred);
                assert(bytes_transferred == 4);
                write_end<isServer>(parent, socket, msg);
            }
        }
        else {
            write_end<isServer>(parent, socket, msg);
        }
    }
    template <bool isServer, class SOCKETTYPE, class SENDBUFFERTYPE>
    inline void write(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, const SENDBUFFERTYPE &msg)
    {
        size_t sendsize = 0;
        unsigned char header[10] = {};

        setFin(header, 0xFF);
        set_MaskBitForSending(header, isServer);
        setOpCode(header, msg.code);
        setrsv1(header, 0x00);
        setrsv2(header, 0x00);
        setrsv3(header, 0x00);

        if (msg.len <= 125) {
            setpayloadLength1(header, hton(static_cast<unsigned char>(msg.len)));
            sendsize = 2;
        }
        else if (msg.len > USHRT_MAX) {
            setpayloadLength8(header, hton(static_cast<unsigned long long int>(msg.len)));
            setpayloadLength1(header, 127);
            sendsize = 10;
        }
        else {
            setpayloadLength2(header, hton(static_cast<unsigned short>(msg.len)));
            setpayloadLength1(header, 126);
            sendsize = 4;
        }

        assert(msg.len < UINT32_MAX);
        writeexpire_from_now<isServer>(parent, socket, parent->WriteTimeout);
        std::error_code ec;
        auto bytes_transferred = asio::write(socket->Socket, asio::buffer(header, sendsize), ec);
        UNUSED(bytes_transferred);
        if (!ec) {
            assert(sendsize == bytes_transferred);
            writeend<isServer>(parent, socket, msg, isServer);
        }
        else {
            handleclose(parent, socket, 1002, "write header failed " + ec.message());
        }
    }
    template <bool isServer, class SOCKETTYPE> inline void startwrite(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket)
    {
        if (!socket->Writing) {
            if (!socket->SendMessageQueue.empty()) {
                socket->Writing = true;
                auto msg(socket->SendMessageQueue.front());
                socket->SendMessageQueue.pop_front();
                write<isServer>(parent, socket, msg.msg);
            }
            else {
                writeexpire_from_now<isServer>(parent, socket, std::chrono::seconds(0)); // make sure the write timer doesnt kick off
            }
        }
    }
    template <bool isServer, class SOCKETTYPE, class SENDBUFFERTYPE>
    void sendImpl(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, const SENDBUFFERTYPE &msg,
                  CompressionOptions compressmessage)
    {
        if (compressmessage == CompressionOptions::COMPRESS) {
            assert(msg.code == OpCode::BINARY || msg.code == OpCode::TEXT);
        }

        socket->Socket.get_io_service().post([socket, msg, parent, compressmessage]() {

            if (socket->SocketStatus_ == SocketStatus::CONNECTED) {
                // update the socket status to reflect it is closing to prevent other messages from being sent.. this is the last valid message
                // make sure to do this after a call to write so the write process sends the close message, but no others
                if (msg.code == OpCode::CLOSE) {
                    socket->SocketStatus_ = SocketStatus::CLOSING;
                }
                socket->Bytes_PendingFlush += msg.len;
                socket->SendMessageQueue.emplace_back(SendQueueItem{msg, compressmessage});
                SL::WS_LITE::startwrite<isServer>(parent, socket);
            }
        });
    }
    template <bool isServer, class SOCKETTYPE>
    void sendclosemessage(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, unsigned short code, const std::string &msg)
    {
        SL_WS_LITE_LOG(Logging_Levels::INFO_log_level, "closeImpl " << msg);
        WSMessage ws;
        ws.code = OpCode::CLOSE;
        ws.len = sizeof(code) + msg.size();
        ws.Buffer = std::shared_ptr<unsigned char>(new unsigned char[ws.len], [](unsigned char *p) { delete[] p; });
        *reinterpret_cast<unsigned short *>(ws.Buffer.get()) = ntoh(code);
        memcpy(ws.Buffer.get() + sizeof(code), msg.c_str(), msg.size());
        ws.data = ws.Buffer.get();
        sendImpl<isServer>(parent, socket, ws, CompressionOptions::NO_COMPRESSION);
    }

    template <class SOCKETTYPE>
    inline void handleclose(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, unsigned short code, const std::string &msg)
    {
        SL_WS_LITE_LOG(Logging_Levels::INFO_log_level, "Closed: " << code);
        socket->SocketStatus_ = SocketStatus::CLOSED;
        socket->Writing = false;
        if (parent->onDisconnection) {
            parent->onDisconnection(socket, code, msg);
        }

        socket->SendMessageQueue.clear(); // clear all outbound messages
        socket->canceltimers();
        std::error_code ec;
        socket->Socket.lowest_layer().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        ec.clear();
        socket->Socket.lowest_layer().close(ec);
    }

    template <bool isServer, class SOCKETTYPE, class SENDBUFFERTYPE>
    void write_end(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, const SENDBUFFERTYPE &msg)
    {

        asio::async_write(socket->Socket, asio::buffer(msg.data, msg.len),
                          [parent, socket, msg](const std::error_code &ec, size_t bytes_transferred) {
                              socket->Writing = false;
                              UNUSED(bytes_transferred);
                              socket->Bytes_PendingFlush -= msg.len;
                              if (msg.code == OpCode::CLOSE) {
                                  // final close.. get out and dont come back mm kay?
                                  return handleclose(parent, socket, 1000, "");
                              }
                              if (ec) {
                                  return handleclose(parent, socket, 1002, "write header failed " + ec.message());
                              }
                              assert(msg.len == bytes_transferred);
                              startwrite<isServer>(parent, socket);
                          });
    }

    inline void UnMaskMessage(size_t readsize, unsigned char *buffer, bool isserver)
    {
        if (isserver) {
            auto startp = buffer;
            unsigned char mask[4] = {};
            memcpy(mask, startp, 4);
            for (size_t c = 4; c < readsize; c++) {
                startp[c - 4] = startp[c] ^ mask[c % 4];
            }
        }
    }

    template <bool isServer, class SOCKETTYPE>
    inline void ProcessMessage(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket,
                               const std::shared_ptr<asio::streambuf> &extradata)
    {

        auto opcode = static_cast<OpCode>(getOpCode(socket->ReceiveHeader));

        if (!getFin(socket->ReceiveHeader)) {
            if (socket->LastOpCode == OpCode::INVALID) {
                if (opcode != OpCode::BINARY && opcode != OpCode::TEXT) {
                    return sendclosemessage<isServer>(parent, socket, 1002, "First Non Fin Frame must be binary or text");
                }
                socket->LastOpCode = opcode;
            }
            else if (opcode != OpCode::CONTINUATION) {
                return sendclosemessage<isServer>(parent, socket, 1002, "Continuation Received without a previous frame");
            }
            return ReadHeaderNext<isServer>(parent, socket, extradata);
        }
        else {

            if (socket->LastOpCode != OpCode::INVALID && opcode != OpCode::CONTINUATION) {
                return sendclosemessage<isServer>(parent, socket, 1002, "Continuation Received without a previous frame");
            }
            else if (socket->LastOpCode == OpCode::INVALID && opcode == OpCode::CONTINUATION) {
                return sendclosemessage<isServer>(parent, socket, 1002, "Continuation Received without a previous frame");
            }
            else if (socket->LastOpCode == OpCode::TEXT || opcode == OpCode::TEXT) {
                if (!isValidUtf8(socket->ReceiveBuffer, socket->ReceiveBufferSize)) {
                    return sendclosemessage<isServer>(parent, socket, 1007, "Frame not valid utf8");
                }
            }
            if (parent->onMessage) {
                auto unpacked =
                    WSMessage{socket->ReceiveBuffer, socket->ReceiveBufferSize, socket->LastOpCode != OpCode::INVALID ? socket->LastOpCode : opcode};
                parent->onMessage(socket, unpacked);
            }
            ReadHeaderStart<isServer>(parent, socket, extradata);
        }
    }
    template <bool isServer, class SOCKETTYPE>
    inline void SendPong(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, const std::shared_ptr<unsigned char> &buffer,
                         size_t size)
    {
        WSMessage msg;
        msg.Buffer = buffer;
        msg.len = size;
        msg.code = OpCode::PONG;
        msg.data = msg.Buffer.get();

        sendImpl<isServer>(parent, socket, msg, CompressionOptions::NO_COMPRESSION);
    }
    template <bool isServer, class SOCKETTYPE>
    inline void ProcessClose(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, const std::shared_ptr<unsigned char> &buffer,
                             size_t size)
    {
        if (size >= 2) {
            auto closecode = hton(*reinterpret_cast<unsigned short *>(buffer.get()));
            if (size > 2) {
                if (!isValidUtf8(buffer.get() + sizeof(closecode), size - sizeof(closecode))) {
                    return sendclosemessage<isServer>(parent, socket, 1007, "Frame not valid utf8");
                }
            }

            if (((closecode >= 1000 && closecode <= 1011) || (closecode >= 3000 && closecode <= 4999)) && closecode != 1004 && closecode != 1005 &&
                closecode != 1006) {
                return sendclosemessage<isServer>(parent, socket, 1000, "");
            }
            else {
                return sendclosemessage<isServer>(parent, socket, 1002, "");
            }
        }
        else if (size != 0) {
            return sendclosemessage<isServer>(parent, socket, 1002, "");
        }
        return sendclosemessage<isServer>(parent, socket, 1000, "");
    }
    template <bool isServer, class SOCKETTYPE>
    inline void ProcessControlMessage(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket,
                                      const std::shared_ptr<unsigned char> &buffer, size_t size, const std::shared_ptr<asio::streambuf> &extradata)
    {
        if (!getFin(socket->ReceiveHeader)) {
            return sendclosemessage<isServer>(parent, socket, 1002, "Closing connection. Control Frames must be Fin");
        }
        auto opcode = static_cast<OpCode>(getOpCode(socket->ReceiveHeader));

        switch (opcode) {
        case OpCode::PING:
            if (parent->onPing) {
                parent->onPing(socket, buffer.get(), size);
            }
            SendPong<isServer>(parent, socket, buffer, size);
            break;
        case OpCode::PONG:
            if (parent->onPong) {
                parent->onPong(socket, buffer.get(), size);
            }
            break;
        case OpCode::CLOSE:
            return ProcessClose<isServer>(parent, socket, buffer, size);

        default:
            return sendclosemessage<isServer>(parent, socket, 1002, "Closing connection. nonvalid op code");
        }
        ReadHeaderNext<isServer>(parent, socket, extradata);
    }

    template <bool isServer, class SOCKETTYPE>
    inline void ReadBody(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, const std::shared_ptr<asio::streambuf> &extradata)
    {
        if (!DidPassMaskRequirement(socket->ReceiveHeader, isServer)) { // Close connection if it did not meet the mask requirement.
            return sendclosemessage<isServer>(parent, socket, 1002, "Closing connection because mask requirement not met");
        }
        if (getrsv2(socket->ReceiveHeader) || getrsv3(socket->ReceiveHeader) || (getrsv1(socket->ReceiveHeader) && !socket->CompressionEnabled)) {
            return sendclosemessage<isServer>(parent, socket, 1002, "Closing connection. rsv bit set");
        }
        auto opcode = static_cast<OpCode>(getOpCode(socket->ReceiveHeader));

        size_t size = getpayloadLength1(socket->ReceiveHeader);
        switch (size) {
        case 126:
            size = ntoh(getpayloadLength2(socket->ReceiveHeader));
            break;
        case 127:
            size = static_cast<size_t>(ntoh(getpayloadLength8(socket->ReceiveHeader)));
            if (size > std::numeric_limits<std::size_t>::max()) {
                return sendclosemessage<isServer>(parent, socket, 1009, "Payload exceeded MaxPayload size");
            }
            break;
        default:
            break;
        }

        size += AdditionalBodyBytesToRead(isServer);
        if (opcode == OpCode::PING || opcode == OpCode::PONG || opcode == OpCode::CLOSE) {
            if (size - AdditionalBodyBytesToRead(isServer) > CONTROLBUFFERMAXSIZE) {
                return sendclosemessage<isServer>(parent, socket, 1002,
                                                  "Payload exceeded for control frames. Size requested " + std::to_string(size));
            }
            else if (size > 0) {
                auto buffer = std::shared_ptr<unsigned char>(new unsigned char[size], [](unsigned char *p) { delete[] p; });

                auto bytestoread = size;
                auto dataconsumed = ReadFromExtraData(buffer.get(), bytestoread, extradata);
                bytestoread -= dataconsumed;

                asio::async_read(socket->Socket, asio::buffer(buffer.get() + dataconsumed, bytestoread),
                                 [size, extradata, parent, socket, buffer](const std::error_code &ec, size_t) {
                                     if (!ec) {
                                         UnMaskMessage(size, buffer.get(), isServer);
                                         auto tempsize = size - AdditionalBodyBytesToRead(isServer);
                                         return ProcessControlMessage<isServer>(parent, socket, buffer, tempsize, extradata);
                                     }
                                     else {
                                         return sendclosemessage<isServer>(parent, socket, 1002, "ReadBody Error " + ec.message());
                                     }
                                 });
            }
            else {
                std::shared_ptr<unsigned char> ptr;
                return ProcessControlMessage<isServer>(parent, socket, ptr, 0, extradata);
            }
        }

        else if (opcode == OpCode::TEXT || opcode == OpCode::BINARY || opcode == OpCode::CONTINUATION) {
            auto addedsize = socket->ReceiveBufferSize + size;
            if (addedsize > std::numeric_limits<std::size_t>::max()) {
                SL_WS_LITE_LOG(Logging_Levels::ERROR_log_level, "payload exceeds memory on system!!! ");
                return sendclosemessage<isServer>(parent, socket, 1009, "Payload exceeded MaxPayload size");
            }
            socket->ReceiveBufferSize = addedsize;

            if (socket->ReceiveBufferSize > parent->MaxPayload) {
                return sendclosemessage<isServer>(parent, socket, 1009, "Payload exceeded MaxPayload size");
            }
            if (socket->ReceiveBufferSize > std::numeric_limits<std::size_t>::max()) {
                SL_WS_LITE_LOG(Logging_Levels::ERROR_log_level, "payload exceeds memory on system!!! ");
                return sendclosemessage<isServer>(parent, socket, 1009, "Payload exceeded MaxPayload size");
            }

            if (size > 0) {
                socket->ReceiveBuffer = static_cast<unsigned char *>(realloc(socket->ReceiveBuffer, socket->ReceiveBufferSize));
                if (!socket->ReceiveBuffer) {
                    SL_WS_LITE_LOG(Logging_Levels::ERROR_log_level, "MEMORY ALLOCATION ERROR!!! Tried to realloc " << socket->ReceiveBufferSize);
                    return sendclosemessage<isServer>(parent, socket, 1009, "Payload exceeded MaxPayload size");
                }

                auto bytestoread = size;
                auto dataconsumed = ReadFromExtraData(socket->ReceiveBuffer + socket->ReceiveBufferSize - size, bytestoread, extradata);
                bytestoread -= dataconsumed;

                asio::async_read(socket->Socket, asio::buffer(socket->ReceiveBuffer + socket->ReceiveBufferSize - size + dataconsumed, bytestoread),
                                 [size, extradata, parent, socket](const std::error_code &ec, size_t) {
                                     if (!ec) {
                                         auto buffer = socket->ReceiveBuffer + socket->ReceiveBufferSize - size;
                                         UnMaskMessage(size, buffer, isServer);
                                         socket->ReceiveBufferSize -= AdditionalBodyBytesToRead(isServer);
                                         return ProcessMessage<isServer>(parent, socket, extradata);
                                     }
                                     else {
                                         return sendclosemessage<isServer>(parent, socket, 1002, "ReadBody Error " + ec.message());
                                     }
                                 });
            }
            else {
                return ProcessMessage<isServer>(parent, socket, extradata);
            }
        }
        else {
            return sendclosemessage<isServer>(parent, socket, 1002, "Closing connection. nonvalid op code");
        }
    }

    template <bool isServer, class SOCKETTYPE>
    void ReadHeaderNext(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, const std::shared_ptr<asio::streambuf> &extradata)
    {
        readexpire_from_now<isServer>(parent, socket, parent->ReadTimeout);
        size_t bytestoread = 2;
        auto dataconsumed = ReadFromExtraData(socket->ReceiveHeader, bytestoread, extradata);
        bytestoread -= dataconsumed;

        asio::async_read(socket->Socket, asio::buffer(socket->ReceiveHeader + dataconsumed, bytestoread),
                         [parent, socket, extradata](const std::error_code &ec, size_t) {
                             if (!ec) {
                                 size_t bytestoread = getpayloadLength1(socket->ReceiveHeader);
                                 switch (bytestoread) {
                                 case 126:
                                     bytestoread = 2;
                                     break;
                                 case 127:
                                     bytestoread = 8;
                                     break;
                                 default:
                                     bytestoread = 0;
                                 }
                                 if (bytestoread > 1) {
                                     auto dataconsumed = ReadFromExtraData(socket->ReceiveHeader + 2, bytestoread, extradata);
                                     bytestoread -= dataconsumed;

                                     asio::async_read(socket->Socket, asio::buffer(socket->ReceiveHeader + 2 + dataconsumed, bytestoread),
                                                      [parent, socket, extradata](const std::error_code &ec, size_t) {
                                                          if (!ec) {
                                                              ReadBody<isServer>(parent, socket, extradata);
                                                          }
                                                          else {
                                                              return sendclosemessage<isServer>(parent, socket, 1002,
                                                                                                "readheader ExtendedPayloadlen " + ec.message());
                                                          }
                                                      });
                                 }
                                 else {
                                     ReadBody<isServer>(parent, socket, extradata);
                                 }
                             }
                             else {
                                 return sendclosemessage<isServer>(parent, socket, 1002, "WebSocket ReadHeader failed " + ec.message());
                             }
                         });
    }
    template <bool isServer, class SOCKETTYPE>
    void ReadHeaderStart(const std::shared_ptr<WSContextImpl> parent, const SOCKETTYPE &socket, const std::shared_ptr<asio::streambuf> &extradata)
    {
        free(socket->ReceiveBuffer);
        socket->ReceiveBuffer = nullptr;
        socket->ReceiveBufferSize = 0;
        socket->LastOpCode = OpCode::INVALID;
        ReadHeaderNext<isServer>(parent, socket, extradata);
    }

    class WSClient final : public IWSHub {
        std::shared_ptr<WSContextImpl> Impl_;

      public:
        WSClient(const std::shared_ptr<WSContextImpl> &c) : Impl_(c) {}
        virtual ~WSClient() {}
        virtual void set_MaxPayload(size_t bytes) override;
        virtual size_t get_MaxPayload() override;
        virtual void set_ReadTimeout(std::chrono::seconds seconds) override;
        virtual std::chrono::seconds get_ReadTimeout() override;
        virtual void set_WriteTimeout(std::chrono::seconds seconds) override;
        virtual std::chrono::seconds get_WriteTimeout() override;
    };
    class WSListener final : public IWSHub {
        std::shared_ptr<WSContextImpl> Impl_;

      public:
        WSListener(const std::shared_ptr<WSContextImpl> &impl) : Impl_(impl) {}
        virtual ~WSListener() {}
        void set_MaxPayload(size_t bytes) override;
        virtual size_t get_MaxPayload() override;
        virtual void set_ReadTimeout(std::chrono::seconds seconds) override;
        virtual std::chrono::seconds get_ReadTimeout() override;
        virtual void set_WriteTimeout(std::chrono::seconds seconds) override;
        virtual std::chrono::seconds get_WriteTimeout() override;
    };

    class WSListener_Configuration final : public IWSListener_Configuration {
        std::shared_ptr<WSContextImpl> Impl_;

      public:
        virtual ~WSListener_Configuration() {}
        WSListener_Configuration(const std::shared_ptr<WSContextImpl> &impl) : Impl_(impl) {}
        virtual std::shared_ptr<IWSListener_Configuration>
        onConnection(const std::function<void(const std::shared_ptr<IWSocket> &, const HttpHeader &)> &handle) override;
        virtual std::shared_ptr<IWSListener_Configuration>
        onMessage(const std::function<void(const std::shared_ptr<IWSocket> &, const WSMessage &)> &handle) override;
        virtual std::shared_ptr<IWSListener_Configuration>
        onDisconnection(const std::function<void(const std::shared_ptr<IWSocket> &, unsigned short, const std::string &)> &handle) override;
        virtual std::shared_ptr<IWSListener_Configuration>
        onPing(const std::function<void(const std::shared_ptr<IWSocket> &, const unsigned char *, size_t)> &handle) override;
        virtual std::shared_ptr<IWSListener_Configuration>
        onPong(const std::function<void(const std::shared_ptr<IWSocket> &, const unsigned char *, size_t)> &handle) override;
        virtual std::shared_ptr<IWSHub> listen(bool no_delay, bool reuse_address) override;
    };

    class WSClient_Configuration final : public IWSClient_Configuration {
        std::shared_ptr<WSContextImpl> Impl_;

      public:
        WSClient_Configuration(const std::shared_ptr<WSContextImpl> &impl) : Impl_(impl) {}
        virtual ~WSClient_Configuration() {}
        virtual std::shared_ptr<IWSClient_Configuration>
        onConnection(const std::function<void(const std::shared_ptr<IWSocket> &, const HttpHeader &)> &handle) override;
        virtual std::shared_ptr<IWSClient_Configuration>
        onMessage(const std::function<void(const std::shared_ptr<IWSocket> &, const WSMessage &)> &handle) override;
        virtual std::shared_ptr<IWSClient_Configuration>
        onDisconnection(const std::function<void(const std::shared_ptr<IWSocket> &, unsigned short, const std::string &)> &handle) override;
        virtual std::shared_ptr<IWSClient_Configuration>
        onPing(const std::function<void(const std::shared_ptr<IWSocket> &, const unsigned char *, size_t)> &handle) override;
        virtual std::shared_ptr<IWSClient_Configuration>
        onPong(const std::function<void(const std::shared_ptr<IWSocket> &, const unsigned char *, size_t)> &handle) override;

        virtual std::shared_ptr<IWSHub> connect(const std::string &host, PortNumber port, bool no_delay, const std::string &endpoint,
                                                const std::unordered_map<std::string, std::string> &extraheaders) override;
    };
    struct DelayedInfo;
} // namespace WS_LITE
} // namespace SL