path = r'c:\proyectos antigravity\webkit PS5 v2\apps\luna-ps5\src\main.cpp'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

text = text.replace('prof.accountName', 'prof.displayName')

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)
print("Replaced successfully.")
