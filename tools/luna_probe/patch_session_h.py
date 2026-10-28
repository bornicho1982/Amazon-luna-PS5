with open("apps/luna-ps5/src/session/luna_session.h", "r", encoding="utf-8") as f:
    c = f.read()

target = "    std::vector<luna::display::LunaCollection> fetchRemotePlayCollections(const std::string& accessToken, const std::string& deviceSerial = \"\");"
replacement = """    std::vector<luna::display::LunaCollection> fetchRemotePlayCollections(const std::string& accessToken, const std::string& deviceSerial = "");
    bool createParty(std::string& outPin, std::string& outPartyId, const std::string& accessToken, const std::string& deviceSerial = "");
    bool joinParty(const std::string& pin, const std::string& accessToken, const std::string& deviceSerial = "");"""

c_norm = c.replace("\r\n", "\n")
t_norm = target.replace("\r\n", "\n")
if t_norm in c_norm:
    c_norm = c_norm.replace(t_norm, replacement.replace("\r\n", "\n"), 1)
    with open("apps/luna-ps5/src/session/luna_session.h", "w", encoding="utf-8") as f:
        f.write(c_norm.replace("\n", "\r\n"))
    print("[+] luna_session.h patched successfully!")
else:
    print("[-] target not found in luna_session.h")
