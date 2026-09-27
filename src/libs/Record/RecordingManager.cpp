#include "RecordingManager.hpp"

#include "Logger.hpp"

#include <iomanip>

RecordingManager::RecordingManager()
    :
    recording_(false)
{
}

void RecordingManager::start(
    const std::string& id,
    int durationSeconds
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (recording_)
    {
        return;
    }

    const std::string sessionDir =
        std::string(PROJECT_ROOT_DIR) +
        "/records/" +
        id;

    std::filesystem::create_directories(
        sessionDir
    );

    Logger::instance().setActiveSessionDir(sessionDir);

    videoFileName_ =
        sessionDir +
        "/sample.avi";

    const std::string csvFileName =
        sessionDir +
        "/coordinates.csv";

    writer_.open(
        videoFileName_,
        cv::VideoWriter::fourcc(
            'M',
            'J',
            'P',
            'G'
        ),
        kTargetFps,
        cv::Size(
            1024,
            690
        ),
        true
    );

    std::cout
        << "[RECORDING] VideoWriter aberto: "
        << writer_.isOpened()
        << std::endl;

    if (!writer_.isOpened())
    {
        Logger::instance().logError(
            "[RECORDING] Nao foi possivel criar " + videoFileName_
        );

        Logger::instance().clearActiveSessionDir();

        return;
    }

    csvFile_.open(
        csvFileName,
        std::ios::out | std::ios::trunc
    );

    if (!csvFile_.is_open())
    {
        Logger::instance().logError(
            "[RECORDING] Nao foi possivel criar " + csvFileName
        );

        writer_.release();

        Logger::instance().clearActiveSessionDir();

        return;
    }

    csvHeaderWritten_ = false;
    frameIndex_ = 0;

    const auto now = std::chrono::steady_clock::now();

    nextFrameDue_ = now;

    endTime_ =
        now +
        std::chrono::seconds(
            durationSeconds
        );

    recording_ = true;

    std::cout
        << "[RECORDING] Iniciada: "
        << sessionDir
        << "\n";
}

void RecordingManager::stop()
{
    std::lock_guard<std::mutex> lock(mutex_);

    stopLocked();
}

void RecordingManager::stopLocked()
{
    if (!recording_)
    {
        return;
    }

    if (writer_.isOpened())
    {
        writer_.release();
    }

    if (csvFile_.is_open())
    {
        csvFile_.close();
    }

    recording_ = false;

    Logger::instance().clearActiveSessionDir();

    // Transcodifica sample.avi (MJPG) para sample.mp4 (H.264,
    // faststart) em background, sem bloquear o proximo start().
    const std::string aviPath = videoFileName_;
    const std::string mp4Path =
        aviPath.substr(0, aviPath.find_last_of('.')) + ".mp4";

    std::thread(
        [aviPath, mp4Path]()
        {
            const std::string cmd =
                "ffmpeg -y -i \"" + aviPath + "\""
                " -c:v libx264 -preset veryfast -crf 23"
                " -movflags +faststart"
                " \"" + mp4Path + "\""
                " >/dev/null 2>&1";

            int result = std::system(cmd.c_str());

            if (result != 0)
            {
                Logger::instance().logError(
                    "RecordingManager: falha ao transcodificar " + aviPath
                );
            }
        }
    ).detach();

    std::cout
        << "[RECORDING] Finalizada.\n";
}

void RecordingManager::writeCsvHeaderLocked(
    const std::vector<WellDetection>& detections
)
{
    csvFile_ << "frame_index";

    for (const WellDetection& detection : detections)
    {
        csvFile_
            << ",x" << detection.wellId
            << ",y" << detection.wellId;
    }

    csvFile_ << "\n";

    csvHeaderWritten_ = true;
}

void RecordingManager::process(
    const cv::Mat& frame,
    const std::vector<WellDetection>& detections
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!recording_)
    {
        return;
    }

    const auto now = std::chrono::steady_clock::now();

    if (now >= endTime_)
    {
        stopLocked();
        return;
    }

    const auto frameInterval =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / kTargetFps)
        );

    // Se a camera estiver mais lenta que o fps alvo, "duplica" o
    // frame atual para preencher os slots de tempo que passaram
    // sem uma captura nova -- garante frames_escritos == kTargetFps
    // x duracao_real independente da camera estar mais rapida OU
    // mais lenta que o fps alvo. kMaxCatchUpFrames evita um loop
    // gigante caso o processamento tenha travado por muito tempo.
    constexpr int kMaxCatchUpFrames = 5;
    int written = 0;

    while (nextFrameDue_ <= now && written < kMaxCatchUpFrames)
    {
        writer_.write(frame);

        if (!csvHeaderWritten_ && !detections.empty())
        {
            writeCsvHeaderLocked(detections);
        }

        if (csvHeaderWritten_)
        {
            csvFile_ << frameIndex_;

            csvFile_ << std::fixed << std::setprecision(2);

            for (const WellDetection& detection : detections)
            {
                csvFile_ << ",";

                if (detection.detected)
                {
                    csvFile_ << detection.x;
                }

                csvFile_ << ",";

                if (detection.detected)
                {
                    csvFile_ << detection.y;
                }
            }

            csvFile_ << "\n";

            csvFile_.flush();
        }

        frameIndex_++;
        nextFrameDue_ += frameInterval;
        written++;
    }

    // Se o atraso acumulado for maior que o limite de catch-up
    // (ex: travamento longo), realinha em vez de tentar recuperar
    // tudo de uma vez.
    if (nextFrameDue_ <= now)
    {
        nextFrameDue_ = now + frameInterval;
    }
}

bool RecordingManager::isRecording() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return recording_;
}