#pragma once

#include "Common.hpp"

// Log de erros centralizado, thread-safe.
//
// Todo erro registrado via logError() sempre vai para o log global
// (records/system.log) - assim nada se perde, mesmo se acontecer
// antes de qualquer gravacao comecar (ex: falha ao abrir camera).
//
// Se houver uma gravacao ativa no momento (ver RecordingManager,
// que chama setActiveSessionDir()/clearActiveSessionDir()), o erro
// TAMBEM e espelhado em records/<id>/error.log - assim quem revisa
// uma sessao especifica ve os erros daquela janela de tempo junto
// com o video/coordinates.csv.
//
// Uso: Logger::instance().logError("[MODULO] mensagem");
class Logger
{
public:

    static Logger& instance();

    // Chamado pelo RecordingManager quando uma gravacao comeca/
    // termina.
    void setActiveSessionDir(const std::string& dir);

    void clearActiveSessionDir();

    // Grava a mensagem com timestamp no log global e, se houver
    // sessao ativa, tambem no log da sessao. Tambem imprime em
    // std::cerr (quem estiver olhando o terminal/journal ao vivo
    // continua vendo, sem precisar duplicar a chamada).
    void logError(const std::string& message);

private:

    Logger();

    std::mutex mutex_;

    std::string activeSessionDir_;

    std::string globalLogPath_;
};
