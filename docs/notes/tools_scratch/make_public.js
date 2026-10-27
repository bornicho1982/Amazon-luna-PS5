const fs = require('fs');
let content = fs.readFileSync('apps/luna-ps5/src/session/luna_session.h', 'utf8');
content = content.replace('    std::vector<luna::display::LunaCollection> fetchRemotePlayCollections(const std::string& accessToken, const std::string& deviceSerial = "");\r\n', '');
content = content.replace('    std::vector<luna::display::LunaCollection> fetchRemotePlayCollections(const std::string& accessToken, const std::string& deviceSerial = "");\n', '');
content = content.replace('    std::string getStatusMessage() const { return m_statusMessage; }',
  '    std::string getStatusMessage() const { return m_statusMessage; }\n    std::vector<luna::display::LunaCollection> fetchRemotePlayCollections(const std::string& accessToken, const std::string& deviceSerial = "");');
fs.writeFileSync('apps/luna-ps5/src/session/luna_session.h', content, 'utf8');
console.log('Made fetchRemotePlayCollections public');
