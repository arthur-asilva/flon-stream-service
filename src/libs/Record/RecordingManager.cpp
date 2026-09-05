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

    // Pasta desta sessao de gravacao: records/<id>/
    const std::string sessionDir =
        std::string(PROJECT_ROOT_DIR) +
        "/records/" +
        id;

    std::filesystem::create_directories(
        sessionDir
    );

    // A partir daqui, qualquer Logger::logError() (vindo desta
    // thread ou de outra, ex: falha de camera durante a gravacao)
    // tambem e espelhado em sessionDir/error.log.
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
        30.0,
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

    // truncate: cada gravacao com este id comeca um
    // coordinates.csv novo (sobrescreve o anterior).
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

    endTime_ =
        std::chrono::steady_clock::now() +
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

    if (
        std::chrono::steady_clock::now()
        >=
        endTime_
    )
    {
        stopLocked();
        return;
    }

    writer_.write(frame);

    // O cabecalho depende de conhecer os wellId presentes
    // (vem de detections). Se o primeiro frame chegar sem
    // deteccoes (layout ainda nao carregado), tenta de
    // novo nos proximos frames ate conseguir.
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
}

bool RecordingManager::isRecording() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return recording_;
}
