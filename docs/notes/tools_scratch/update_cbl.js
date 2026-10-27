const fs = require('fs');

let h = fs.readFileSync('apps/luna-ps5/src/auth/cbl_client.h', 'utf8');
h = h.replace('std::string getVerificationUri() const { return "amazon.com/code"; }',
  'void setRegionDomain(const std::string& domain);\n    std::string getVerificationUri() const { return m_verificationUri; }');
h = h.replace('std::string m_deviceSerial;',
  'std::string m_deviceSerial;\n    std::string m_domain = "amazon.es";\n    std::string m_verificationUri = "amazon.es/code";');
fs.writeFileSync('apps/luna-ps5/src/auth/cbl_client.h', h, 'utf8');

let c = fs.readFileSync('apps/luna-ps5/src/auth/cbl_client.cpp', 'utf8');
c = c.replace('void CblClient::generateQrCode() {\n    std::string qrText = "https://amazon.com/code";',
  'void CblClient::setRegionDomain(const std::string& domain) {\n    m_domain = domain;\n    m_verificationUri = domain + "/code";\n    generateQrCode();\n}\n\nvoid CblClient::generateQrCode() {\n    std::string qrText = "https://" + m_verificationUri;');
fs.writeFileSync('apps/luna-ps5/src/auth/cbl_client.cpp', c, 'utf8');
console.log('Successfully updated cbl_client.h and cbl_client.cpp');
