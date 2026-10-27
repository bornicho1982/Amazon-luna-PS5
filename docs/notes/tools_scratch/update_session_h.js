const fs = require('fs');

let h = fs.readFileSync('apps/luna-ps5/src/session/luna_session.h', 'utf8');

h = h.replace('const std::vector<luna::display::LunaCollection>& getLibraryCollections() const { return m_libraryCollections; }',
  'const std::vector<luna::display::LunaCollection>& getLibraryCollections() const { return m_libraryCollections; }\n    const std::vector<luna::display::LunaCollection>& getRemotePlayCollections() const { return m_remotePlayCollections; }');

h = h.replace('std::vector<luna::display::LunaCollection> m_libraryCollections;',
  'std::vector<luna::display::LunaCollection> m_libraryCollections;\n    std::vector<luna::display::LunaCollection> m_remotePlayCollections;');

h = h.replace('std::vector<luna::display::LunaCollection> fetchPageCollections(',
  'std::vector<luna::display::LunaCollection> fetchRemotePlayCollections(const std::string& accessToken, const std::string& deviceSerial = "");\n    std::vector<luna::display::LunaCollection> fetchPageCollections(');

fs.writeFileSync('apps/luna-ps5/src/session/luna_session.h', h, 'utf8');
console.log('Updated luna_session.h');
