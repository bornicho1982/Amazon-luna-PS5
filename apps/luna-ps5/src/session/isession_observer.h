#pragma once

#include <string>
#include <vector>

// Forward declarations to avoid circular dependencies
namespace luna {
namespace display {
    struct GameTile;
}
namespace session {
    class LunaSessionClient;
    enum class SessionState;
    struct GameSessionInfo;
}
}

namespace luna::session {

/**
 * @brief Interface para observers que reciben notificaciones de cambios en el estado de sesión
 * 
 * Patrón Observer: desacopla LunaSessionClient de DisplayManager y otros componentes
 */
class ISessionObserver {
public:
    virtual ~ISessionObserver() = default;
    
    /**
     * @brief Notifica cambio en estado de sesión
     * @param oldState Estado anterior
     * @param newState Nuevo estado
     */
    virtual void onSessionStateChanged(SessionState oldState, SessionState newState) = 0;
    
    /**
     * @brief Notifica que información de sesión está lista
     * @param Info completa de la sesión activa
     */
    virtual void onSessionInfoReady(const GameSessionInfo& info) = 0;
    
    /**
     * @brief Notifica actualización de colecciones (home/library)
     * @param home Colección de juegos destacados del home screen
     * @param library Biblioteca completa del usuario
     */
    virtual void onCollectionsUpdated(
        const std::vector<luna::display::GameTile>& home,
        const std::vector<luna::display::GameTile>& library
    ) = 0;
    
    /**
     * @brief Notifica cambio en estado de autenticación
     * @param authenticated true si usuario está autenticado
     */
    virtual void onAuthStateChanged(bool authenticated) = 0;
    
    /**
     * @brief Notifica inicio de streaming de juego
     * @param Game tile que se está streamando
     */
    virtual void onStreamingStarted(const luna::display::GameTile& game) = 0;
    
    /**
     * @brief Notifica fin de streaming
     */
    virtual void onStreamingStopped() = 0;
    
    /**
     * @brief Notifica error en sesión
     * @param error Código de error
     * @param message Mensaje descriptivo
     */
    virtual void onError(int errorCode, const std::string& message) = 0;
};

} // namespace luna::session
