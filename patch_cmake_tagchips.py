p='CMakeLists.txt'
s=open(p,encoding='utf-8').read()
# Add source pair after peoplecardutils in targets (two occurrences)
needle='    src/ui/people/peoplecardutils.h\n    src/ui/people/peoplecardutils.cpp\n'
repl=needle+'    src/ui/people/tagchiputils.h\n    src/ui/people/tagchiputils.cpp\n'
assert s.count(needle)==2
s=s.replace(needle,repl,2)
open(p,'w',encoding='utf-8').write(s)
print('done')
PY