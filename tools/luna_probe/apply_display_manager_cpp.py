import os
import sys

# Import the new sections
import display_sections

with open("apps/luna-ps5/src/display/display_manager.cpp", "r", encoding="utf-8") as f:
    orig = f.read()

# Indices
idx_home = orig.find("void DisplayManager::drawLunaHomeScreen(")
idx_detail = orig.find("void DisplayManager::drawGameDetailScreen(")
idx_waiting = orig.find("void DisplayManager::drawNativeEngineWaitingScreen(")
idx_settings = orig.find("void DisplayManager::drawSettingsScreen(")
idx_media = orig.find("void DisplayManager::drawMediaViewerScreen(")
idx_present = orig.find("void DisplayManager::present() {")

if -1 in [idx_home, idx_detail, idx_waiting, idx_settings, idx_media, idx_present]:
    print("Error: Could not find all section boundaries!")
    sys.exit(1)

# Assemble new code
part1 = orig[:idx_home]
part2 = display_sections.new_drawLunaHomeScreen + "\n"
part3 = display_sections.new_drawGameDetailScreen + "\n"
part4 = orig[idx_waiting:idx_settings]
part5 = display_sections.new_drawSettingsScreen + "\n"
part6 = display_sections.new_drawMediaViewerScreen + "\n"
part7 = display_sections.new_helpers + "\n"
part8 = orig[idx_present:]

new_full = part1 + part2 + part3 + part4 + part5 + part6 + part7 + part8

# Write back
with open("apps/luna-ps5/src/display/display_manager.cpp", "w", encoding="utf-8") as out:
    out.write(new_full.replace("\r\n", "\n").replace("\n", "\r\n"))

print(f"[+] display_manager.cpp assembled successfully! Size: {len(new_full)} chars")
