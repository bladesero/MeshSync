#pragma once

#include <list>
#include <map>
#include <mutex>
#include <future>

#include "MeshSync/msProtocol.h"
#include "MeshSync/SceneGraph/msSceneImportSettings.h"

namespace Poco {
    namespace Net {
        class HTTPServer;
        class HTTPServerRequest;
        class HTTPServerResponse;
    }
}

//Fwd Declarations
msDeclClassPtr(Message)
msDeclClassPtr(PollMessage)
msDeclClassPtr(SetMessage)
msDeclClassPtr(GetMessage)
msDeclClassPtr(ScreenshotMessage)
msDeclClassPtr(ServerLiveEditRequest)
msDeclClassPtr(PropertyInfo)
msDeclClassPtr(Curve)

namespace ms {

struct ServerSettings
{
    int max_queue = 256;
    int max_threads = 8;
    uint16_t port = 8080;

    SceneImportSettings import_settings;
};

class Server {
public:
    Server(const ServerSettings& settings);
    ~Server();

    bool start();
    void stop();
    void clear();
    ServerSettings& getSettings();

    inline const bool IsPublicAccessAllowed();
    inline void AllowPublicAccess(const bool access);

    using MessageHandler = std::function<void(Message::Type type, Message& data)>;
    int getNumMessages() const;
    int processMessages(const MessageHandler& handler);

    void serveText(Poco::Net::HTTPServerResponse &response, const char* text, int stat = 200);
    void serveBinary(Poco::Net::HTTPServerResponse &response, const void *data, size_t size, int stat = 200);
    void serveFiles(Poco::Net::HTTPServerResponse &response, const std::string& uri);

    void setServe(bool v);
    bool isServing() const;

    void beginServeScene();
    void endServeScene();

    void setScrrenshotFilePath(const std::string& path);
    void setFileRootPath(const std::string& path);

    void notifyPoll(PollMessage::PollType t);

public:
    struct MessageHolder
    {
        MessagePtr message;
        std::future<void> task;
        std::atomic_bool ready = { false };

        MessageHolder();
        MessageHolder(MessageHolder&& v);
    };

    Scene* getHostScene();
    void queueTextMessage(const char *mes, TextMessage::Type type);
    void recvSet(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response);
    void recvDelete(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response);
    void recvFence(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response);
    void recvGet(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response);
    void recvQuery(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response);
    void recvText(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response);
    void recvScreenshot(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response);
    void recvPoll(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response);
    void recvServerLiveEditRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response);
    
    void receivedProperty(PropertyInfoPtr prop);
    void syncRequested();
    void propertiesReady();
    bool readyForProperties();

    static void sanitizeHierarchyPath(std::string& path);

private:
    template<class MessageT>
    std::shared_ptr<MessageT> deserializeMessage(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response);

    MessageHolder* queueMessage(MessagePtr mes);
    MessageHolder* queueMessage(MessagePtr mes, std::future<void>&& task);

    bool loadMIMETypes(const std::string& path);
    const std::string& getMIMEType(const std::string& filename);
    inline const std::string& GetFileRootPath() const;

private:
    using HTTPServerPtr = std::shared_ptr<Poco::Net::HTTPServer>;
    using lock_t = std::unique_lock<std::mutex>;
    using PollMessages = std::vector<PollMessagePtr>;

    bool m_serving = true;
    bool m_allowPublicAccess = false;
    ServerSettings m_settings;
    HTTPServerPtr m_server;
    std::map<std::string, std::string> m_mimetypes;
    std::mutex m_message_mutex;
    std::mutex m_poll_mutex;

    int m_current_scene_session = InvalidID;
    std::list<MessageHolder> m_received_messages, m_processing_messages;
    std::vector<SetMessagePtr> m_scene_cache;
    PollMessages m_polls;

    ScenePtr m_host_scene;
    GetMessagePtr m_current_get_request;
    ScreenshotMessagePtr m_current_screenshot_request;
    std::mutex m_properties_mutex;
    ServerLiveEditRequestPtr m_current_live_edit_request;
    std::atomic_bool m_syncRequested;
    std::string m_screenshot_file_path;
    std::string m_file_root_path;

    public:
    std::vector<EntityPtr> m_pending_entities;
    std::map<uint64_t, PropertyInfoPtr> m_pending_properties;

    template<typename T>
    T* getOrCreatePendingEntity(const char* path) {
        for (size_t i = 0; i < m_pending_entities.size(); i++) {
            if (m_pending_entities[i]->path == path) {
                return dynamic_cast<T*>(m_pending_entities[i].get());
            }
        }

        // If it doesn't exist, add it:
        shared_ptr<T> result = T::create();
        result->path = path;
        m_pending_entities.push_back(result);

        return result.get();
    }
};

//----------------------------------------------------------------------------------------------------------------------

const std::string& Server::GetFileRootPath() const { return m_file_root_path; }
const bool Server::IsPublicAccessAllowed() { return m_allowPublicAccess; }
inline void Server::AllowPublicAccess(const bool access) { m_allowPublicAccess = access; }


} // namespace ms

