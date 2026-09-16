#include "MjpegServer.hpp"

#include "Logger.hpp"

#include <algorithm>
#include <cctype>

MjpegServer::MjpegServer(
    FrameStore& frameStore,
    CameraCapture& cameraCapture,
    RecordingManager& recordingManager,
    std::atomic<bool>& running,
    int port
)
:
frameStore_(frameStore),
cameraCapture_(cameraCapture),
recordingManager_(recordingManager),
running_(running),
port_(port)
{
}

int MjpegServer::getQueryInt(
const std::string& request,
const std::string& parameterName
) const
{
const std::size_t firstLineEnd = request.find("\r\n");

if (firstLineEnd == std::string::npos)
    return 0;

const std::string firstLine =
    request.substr(0, firstLineEnd);

const std::string key =
    parameterName + "=";

const std::size_t parameterPosition =
    firstLine.find(key);

if (parameterPosition == std::string::npos)
    return 0;

const std::size_t valueStart =
    parameterPosition + key.size();

std::size_t valueEnd =
    firstLine.find('&', valueStart);

if (valueEnd == std::string::npos)
    valueEnd = firstLine.find(' ', valueStart);

if (valueEnd == std::string::npos ||
    valueEnd <= valueStart)
{
    return 0;
}

try
{
    return std::stoi(
        firstLine.substr(
            valueStart,
            valueEnd - valueStart
        )
    );
}
catch (...)
{
    return 0;
}

}

std::string MjpegServer::getQueryString(
    const std::string& request,
    const std::string& parameterName
) const
{
    const std::size_t firstLineEnd =
        request.find("\r\n");

    if (firstLineEnd == std::string::npos)
        return "";

    const std::string firstLine =
        request.substr(0, firstLineEnd);

    const std::string key =
        parameterName + "=";

    const std::size_t parameterPosition =
        firstLine.find(key);

    if (parameterPosition == std::string::npos)
        return "";

    const std::size_t valueStart =
        parameterPosition + key.size();

    std::size_t valueEnd =
        firstLine.find('&', valueStart);

    if (valueEnd == std::string::npos)
        valueEnd = firstLine.find(' ', valueStart);

    if (valueEnd == std::string::npos ||
        valueEnd <= valueStart)
    {
        return "";
    }

    return firstLine.substr(
        valueStart,
        valueEnd - valueStart
    );
}

bool MjpegServer::hasQueryParam(
    const std::string& request,
    const std::string& parameterName
) const
{
    const std::size_t firstLineEnd =
        request.find("\r\n");

    if (firstLineEnd == std::string::npos)
        return false;

    const std::string firstLine =
        request.substr(0, firstLineEnd);

    const std::string key =
        parameterName + "=";

    return firstLine.find(key) != std::string::npos;
}

MjpegServer::StreamParams
MjpegServer::getStreamParams(
const std::string& request
) const
{
StreamParams params;

params.id = getQueryString(request, "id");

// id precisa ser um inteiro (usado como nome da pasta
// records/<id>/). Qualquer coisa fora de 0-9, ou vazio,
// invalida o id e a gravacao nao inicia.
if (
    params.id.empty() ||
    !std::all_of(
        params.id.begin(),
        params.id.end(),
        [](unsigned char c) { return std::isdigit(c); }
    )
)
{
    params.id.clear();
}

params.durationSeconds =
    getQueryInt(request, "duration");

params.wellsCount =
    getQueryInt(request, "wells_count");

// Distingue "chave ausente da URL" (nao mexe na configuracao
// atual) de "presente, mesmo que invalida/zero" (pedido
// explicito de ver sem mascara - ver uso em MjpegServer::run()).
params.wellsCountProvided =
    hasQueryParam(request, "wells_count");

if (params.durationSeconds < 1 ||
    params.durationSeconds > 3600)
{
    params.durationSeconds = 0;
}

if (
    params.wellsCount != 6 &&
    params.wellsCount != 12 &&
    params.wellsCount != 24 &&
    params.wellsCount != 96
)
{
    params.wellsCount = 0;
}

return params;

}

void MjpegServer::clientThread(
    int clientFd
)
{
std::cout
<< "[STREAM] Cliente iniciado\n";

const std::string header =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
    "Cache-Control: no-cache\r\n"
    "Connection: close\r\n\r\n";

if (
    send(
        clientFd,
        header.c_str(),
        static_cast<int>(header.size()),
        MSG_NOSIGNAL
    ) < 0
)
{
    close(clientFd);
    return;
}


while (running_)
{

    auto jpeg =
        frameStore_.getLatest();

    if (!jpeg || jpeg->empty())
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(5));

        continue;
    }

    std::ostringstream partHeader;

    partHeader
        << "--frame\r\n"
        << "Content-Type: image/jpeg\r\n"
        << "Content-Length: "
        << jpeg->size()
        << "\r\n\r\n";

    const std::string part =
        partHeader.str();

    if (
        send(
            clientFd,
            part.c_str(),
            static_cast<int>(part.size()),
            MSG_NOSIGNAL
        ) < 0
    )
    {
        break;
    }

    if (
        send(
            clientFd,
            reinterpret_cast<const char*>(
                jpeg->data()
            ),
            static_cast<int>(
                jpeg->size()
            ),
            MSG_NOSIGNAL
        ) < 0
    )
    {
        break;
    }

    static const std::string tail = "\r\n";

    if (
        send(
            clientFd,
            tail.c_str(),
            static_cast<int>(tail.size()),
            MSG_NOSIGNAL
        ) < 0
    )
    {
        break;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(33));
}

close(clientFd);

std::cout
    << "[STREAM] Cliente desconectado.\n";

}

void MjpegServer::run()
{
const int serverFd =
socket(AF_INET, SOCK_STREAM, 0);

if (serverFd < 0)
{
    Logger::instance().logError(
        "[HTTP] Falha ao criar socket."
    );

    running_ = false;
    return;
}

int opt = 1;

#ifdef _WIN32

setsockopt(
    serverFd,
    SOL_SOCKET,
    SO_REUSEADDR,
    reinterpret_cast<const char*>(&opt),
    sizeof(opt)
);

#else

setsockopt(
    serverFd,
    SOL_SOCKET,
    SO_REUSEADDR,
    &opt,
    sizeof(opt)
);

#endif

sockaddr_in address{};

address.sin_family = AF_INET;
address.sin_addr.s_addr = INADDR_ANY;
address.sin_port = htons(port_);

if (
    bind(
        serverFd,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address)
    ) < 0
)
{
    Logger::instance().logError(
        "[HTTP] Falha ao fazer bind."
    );

    close(serverFd);
    running_ = false;
    return;
}

if (listen(serverFd, 8) < 0)
{
    Logger::instance().logError(
        "[HTTP] Falha ao escutar na porta " + std::to_string(port_) + "."
    );

    close(serverFd);
    running_ = false;
    return;
}

std::cout
    << "[HTTP] Servidor iniciado.\n";

while (running_)
{
    sockaddr_in clientAddress{};

#ifdef _WIN32
int clientLength =
sizeof(clientAddress);
#else
socklen_t clientLength =
sizeof(clientAddress);
#endif

    const int clientFd =
        accept(
            serverFd,
            reinterpret_cast<sockaddr*>(
                &clientAddress
            ),
            &clientLength
        );

    if (clientFd < 0)
        continue;

    char requestBuffer[4096]{};

    const int bytesRead =
        recv(
            clientFd,
            requestBuffer,
            sizeof(requestBuffer)-1,
            0
        );

    if (bytesRead <= 0)
    {
        close(clientFd);
        continue;
    }

    const std::string request(
        requestBuffer,
        bytesRead
    );

    const StreamParams params =
        getStreamParams(request);

    // So atualiza a configuracao global se o request trouxe a
    // CHAVE wells_count na URL - mesmo que com valor invalido/0.
    // Isso distingue duas situacoes:
    //   - chave ausente (ex: /favicon.ico, ou qualquer request
    //     paralelo que caia nesta mesma porta sem querer) -> NAO
    //     mexe na configuracao atual, protege contra reset
    //     acidental do estado de rastreio de todos os pocos.
    //   - chave presente mas invalida/0 (ex: ?wells_count=0, ou
    //     so ?id=1&duration=20 sem duracao valida de layout) ->
    //     pedido EXPLICITO de ver o frame sem mascara, util para
    //     conferir visualmente o alinhamento das cameras.
    if(params.wellsCountProvided)
    {
        cameraCapture_.setWellsCount(
            params.wellsCount
        );
    }

    if(
        !params.id.empty() &&
        params.durationSeconds > 0
    )
    {
        recordingManager_.start(
            params.id,
            params.durationSeconds
        );
    }

    std::thread(
        &MjpegServer::clientThread,
        this,
        clientFd
    ).detach();
}

close(serverFd);

}
