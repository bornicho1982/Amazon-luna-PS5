with open('apps/luna-ps5/src/session/luna_session.cpp', 'rb') as f:
    d = f.read()
d = d.replace(b'" Jugar ahora"', b'"Jugar ahora"')
with open('apps/luna-ps5/src/session/luna_session.cpp', 'wb') as f:
    f.write(d)
print("Done")
