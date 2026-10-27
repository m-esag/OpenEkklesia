p='src/database/databasemanager.h'
s=open(p,encoding='utf-8').read()
s=s.replace('    static constexpr int kLatestSchemaVersion = 25;','    static constexpr int kLatestSchemaVersion = 26;',1)
open(p,'w',encoding='utf-8').write(s)
print('ok')
PY