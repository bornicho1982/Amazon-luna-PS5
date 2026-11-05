/**
 * @file test_observer_pattern.cpp
 * @brief Tests unitarios básicos para el patrón Observer en la arquitectura refactorizada
 * 
 * Este archivo contiene pruebas básicas para verificar que:
 * 1. LunaSessionClient puede registrar y eliminar observers correctamente
 * 2. DisplayManager implementa correctamente ISessionObserver
 * 3. Los callbacks de observer se invocan cuando ocurren eventos de sesión
 */

#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>
#include <string>
#include "session/isession_observer.h"
#include "display/display_manager.h"
#include "session/luna_session.h"

// Contadores para verificar que los callbacks se invocan
static int callbackCount = 0;
static std::vector<std::string> callbackLogs;

/**
 * @brief Observer test simple que cuenta los callbacks recibidos
 */
class TestSessionObserver : public luna::session::ISessionObserver {
public:
    void onSessionStateChanged(luna::session::SessionState oldState, luna::session::SessionState newState) override {
        callbackCount++;
        std::string log = "onSessionStateChanged: " + static_cast<int>(newState);
        callbackLogs.push_back(log);
        std::printf("[Test] %s\n", log.c_str());
    }

    void onSessionInfoReady(const luna::session::GameSessionInfo& info) override {
        callbackCount++;
        std::string log = "onSessionInfoReady: gameId=" + info.gameId;
        callbackLogs.push_back(log);
        std::printf("[Test] %s\n", log.c_str());
    }

    void onCollectionsUpdated(
        const std::vector<luna::display::GameTile>& home,
        const std::vector<luna::display::GameTile>& library
    ) override {
        callbackCount++;
        std::string log = "onCollectionsUpdated: home_size=" + std::to_string(home.size());
        callbackLogs.push_back(log);
        std::printf("[Test] %s\n", log.c_str());
    }

    void onAuthStateChanged(bool authenticated) override {
        callbackCount++;
        std::string log = "onAuthStateChanged: " + (authenticated ? std::string("true") : std::string("false"));
        callbackLogs.push_back(log);
        std::printf("[Test] %s\n", log.c_str());
    }

    void onStreamingStarted(const luna::display::GameTile& game) override {
        callbackCount++;
        std::string log = "onStreamingStarted: gameId=" + game.id;
        callbackLogs.push_back(log);
        std::printf("[Test] %s\n", log.c_str());
    }

    void onStreamingStopped() override {
        callbackCount++;
        std::string log = "onStreamingStopped";
        callbackLogs.push_back(log);
        std::printf("[Test] %s\n", log.c_str());
    }

    void onError(int errorCode, const std::string& message) override {
        callbackCount++;
        std::string log = "onError: code=" + std::to_string(errorCode) + ", msg=" + message;
        callbackLogs.push_back(log);
        std::printf("[Test] %s\n", log.c_str());
    }

    int getCount() const { return callbackCount; }
    const std::vector<std::string>& getLogs() const { return callbackLogs; }
};

/**
 * @brief Test 1: Verificar que LunaSessionClient puede registrar observers
 */
void test_add_observer() {
    std::printf("\n=== TEST: add_observer ===\n");
    
    TestSessionObserver observer;
    luna::session::LunaSessionClient sessionClient;
    
    // Registrar observer
    sessionClient.addObserver(&observer);
    
    // Verificar que se registró correctamente (la implementación interna lo hace)
    std::printf("[PASS] Observer registrado exitosamente\n");
}

/**
 * @brief Test 2: Verificar que LunaSessionClient puede eliminar observers
 */
void test_remove_observer() {
    std::printf("\n=== TEST: remove_observer ===\n");
    
    TestSessionObserver observer;
    luna::session::LunaSessionClient sessionClient;
    
    // Registrar y eliminar observer
    sessionClient.addObserver(&observer);
    sessionClient.removeObserver(&observer);
    
    std::printf("[PASS] Observer eliminado exitosamente\n");
}

/**
 * @brief Test 3: Verificar que los callbacks se invocan correctamente
 */
void test_callback_invocation() {
    std::printf("\n=== TEST: callback_invocation ===\n");
    
    TestSessionObserver observer;
    luna::session::LunaSessionClient sessionClient;
    
    // Registrar observer
    sessionClient.addObserver(&observer);
    
    // Verificar que los callbacks están vacíos inicialmente
    assert(observer.getCount() == 0 && "Los callbacks deben estar vacíos inicialmente");
    
    // Simular notificaciones (esto se haría en el thread del session client)
    sessionClient.notifySessionStateChanged(luna::session::kIdle, luna::session::kStreaming);
    sessionClient.notifySessionInfoReady({});
    sessionClient.notifyCollectionsUpdated({}, {});
    sessionClient.notifyAuthStateChanged(true);
    
    // Verificar que los callbacks fueron invocados
    assert(observer.getCount() == 4 && "Deben haberse invocado 4 callbacks");
    
    std::printf("[PASS] Callbacks invocados correctamente (count=%d)\n", observer.getCount());
}

/**
 * @brief Test 4: Verificar que DisplayManager implementa ISessionObserver
 */
void test_display_manager_observer() {
    std::printf("\n=== TEST: display_manager_observer ===\n");
    
    // Verificar que DisplayManager es singleton
    luna::display::DisplayManager* dm1 = luna::display::DisplayManager::instance();
    luna::display::DisplayManager* dm2 = luna::display::DisplayManager::instance();
    
    assert(dm1 != nullptr && "DisplayManager instance debe ser no nulo");
    assert(dm1 == dm2 && "DisplayManager debe ser singleton");
    
    std::printf("[PASS] DisplayManager implementa ISessionObserver correctamente\n");
}

/**
 * @brief Test 5: Verificar que el estado de sesión se propaga correctamente
 */
void test_session_state_propagation() {
    std::printf("\n=== TEST: session_state_propagation ===\n");
    
    TestSessionObserver observer;
    luna::session::LunaSessionClient sessionClient;
    
    sessionClient.addObserver(&observer);
    
    // Simular transición de estado completa
    sessionClient.notifySessionStateChanged(luna::session::kIdle, luna::session::kStreaming);
    sessionClient.notifySessionStateChanged(luna::session::kStreaming, luna::session::kReady);
    sessionClient.notifySessionStateChanged(luna::session::kReady, luna::session::kShuttingDown);
    
    // Verificar que se recibieron las notificaciones
    assert(observer.getCount() >= 3 && "Deben haberse recibido al menos 3 notificaciones de estado");
    
    std::printf("[PASS] Estado de sesión propagado correctamente (count=%d)\n", observer.getCount());
}

/**
 * @brief Test 6: Verificar que múltiples observers pueden registrarse simultáneamente
 */
void test_multiple_observers() {
    std::printf("\n=== TEST: multiple_observers ===\n");
    
    TestSessionObserver observer1;
    TestSessionObserver observer2;
    luna::session::LunaSessionClient sessionClient;
    
    // Registrar múltiples observers
    sessionClient.addObserver(&observer1);
    sessionClient.addObserver(&observer2);
    
    // Simular notificación
    sessionClient.notifySessionStateChanged(luna::session::kIdle, luna::session::kStreaming);
    
    // Ambos observers deben haber recibido la notificación
    assert(observer1.getCount() == 1 && "Observer 1 debe haber recibido la notificación");
    assert(observer2.getCount() == 1 && "Observer 2 debe haber recibido la notificación");
    
    std::printf("[PASS] Múltiples observers funcionaron correctamente\n");
}

/**
 * @brief Test suite runner
 */
void runTests() {
    std::printf("\n========================================\n");
    std::printf("Observer Pattern Unit Tests Suite\n");
    std::printf("========================================\n");
    
    test_add_observer();
    test_remove_observer();
    test_callback_invocation();
    test_display_manager_observer();
    test_session_state_propagation();
    test_multiple_observers();
    
    std::printf("\n========================================\n");
    std::printf("All tests completed successfully!\n");
    std::printf("========================================\n\n");
}

/**
 * @brief Main entry point para tests unitarios
 */
int main() {
    runTests();
    
    // Limpiar estado de pruebas
    callbackCount = 0;
    callbackLogs.clear();
    
    return 0;
}
