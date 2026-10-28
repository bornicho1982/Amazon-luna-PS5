import re

with open("apps/luna-ps5/src/display/display_manager.cpp", "r", encoding="utf-8") as f:
    orig = f.read()

# Let's inspect where drawLunaHomeScreen starts and where drawGameDetailScreen ends
idx_home = orig.find("void DisplayManager::drawLunaHomeScreen(")
idx_detail = orig.find("void DisplayManager::drawGameDetailScreen(")
idx_waiting = orig.find("void DisplayManager::drawNativeEngineWaitingScreen(")
idx_settings = orig.find("void DisplayManager::drawSettingsScreen(")
idx_media = orig.find("void DisplayManager::drawMediaViewerScreen(")
idx_present = orig.find("void DisplayManager::present() {")

print(f"Indices found: home={idx_home}, detail={idx_detail}, waiting={idx_waiting}, settings={idx_settings}, media={idx_media}, present={idx_present}")
