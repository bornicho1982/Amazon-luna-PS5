#!/usr/bin/env python3

path = "apps/luna-ps5/src/net/http_client.cpp"
with open(path, "r", encoding="utf-8") as f:
    content = f.read()

content = content.replace("\r\n", "\n")

old_pools = """    g_netPoolId = sceNetPoolCreate("luna_net", 256 * 1024, 0);
    if (g_netPoolId < 0) {
        char dynamicName[32];
        std::snprintf(dynamicName, sizeof(dynamicName), "luna_%u", static_cast<unsigned>(time(nullptr) & 0xFFFF));
        g_netPoolId = sceNetPoolCreate(dynamicName, 256 * 1024, 0);
    }

    g_sslCtxId = sceSslInit(512 * 1024);
    if (g_sslCtxId < 0) {
        g_sslCtxId = sceSslInit(256 * 1024);
    }

    g_httpCtxId = sceHttpInit(g_netPoolId, g_sslCtxId, 512 * 1024);"""

new_pools = """    g_netPoolId = sceNetPoolCreate("luna_net", 1024 * 1024, 0);
    if (g_netPoolId < 0) {
        char dynamicName[32];
        std::snprintf(dynamicName, sizeof(dynamicName), "luna_%u", static_cast<unsigned>(time(nullptr) & 0xFFFF));
        g_netPoolId = sceNetPoolCreate(dynamicName, 1024 * 1024, 0);
    }

    g_sslCtxId = sceSslInit(1024 * 1024);
    if (g_sslCtxId < 0) {
        g_sslCtxId = sceSslInit(512 * 1024);
    }

    g_httpCtxId = sceHttpInit(g_netPoolId, g_sslCtxId, 1024 * 1024);"""

if old_pools in content:
    content = content.replace(old_pools, new_pools)
    print("1 MB pools updated successfully!")
else:
    print("old_pools not found")

with open(path, "w", encoding="utf-8") as f:
    f.write(content)
