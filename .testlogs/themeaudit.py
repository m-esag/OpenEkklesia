import re, sys

def lum(h):
    h = h.lstrip('#')
    r, g, b = (int(h[i:i+2], 16)/255 for i in (0, 2, 4))
    def f(c): return c/12.92 if c <= 0.03928 else ((c+0.055)/1.055)**2.4
    return 0.2126*f(r) + 0.7152*f(g) + 0.0722*f(b)
def ratio(a, b):
    l1, l2 = sorted((lum(a), lum(b)), reverse=True)
    return (l1+0.05)/(l2+0.05)

src = open('/home/manuel/Documents/OpenEkklesia/src/ui/core/thememanager.cpp').read()
blocks = re.findall(r'(\w+)\.id = "([^"]+)";\s*\n\s*\w+\.name = "([^"]+)";(.*?)(?=\n\s*\w+\.id = |\n    m_registeredThemes\.insert)', src, re.S)
themes = []
for var, tid, tname, body in blocks:
    toks = {}
    for m in re.finditer(r'\w+\.(bgBase|bgSurface|bgOverlay|bgInput|textPrimary|textSecondary|textMuted|accentPrimary|accentSuccess|accentWarning|accentDanger|borderColor) = QColor\("#([0-9a-fA-F]{6})"\)', body):
        toks[m.group(1)] = m.group(2)
    if toks and 'textPrimary' in toks and 'bgSurface' in toks:
        themes.append((tid, 'isDark=true' in body, toks))

print(f"{'theme':<26} {'dark':<4} {'body':>5} {'sec':>5} {'mut':>5} {'accOnB':>6}  issues")
fails = 0
for tid, is_dark, tok in themes:
    body = ratio(tok['textPrimary'], tok['bgBase'])
    sec  = ratio(tok['textSecondary'], tok['bgSurface'])
    mut  = ratio(tok['textMuted'], tok['bgSurface'])
    acc  = ratio(tok['accentPrimary'], tok['bgSurface'])
    issues = []
    if body < 4.5: issues.append(f"BODY {body:.2f}")
    if mut  < 3.0: issues.append(f"MUTED {mut:.2f}")
    if sec  < 3.0: issues.append(f"SEC {sec:.2f}")
    if issues: fails += 1
    print(f"{tid:<26} {str(is_dark):<4} {body:>5.2f} {sec:>5.2f} {mut:>5.2f} {acc:>6.2f}  {'; '.join(issues)}")
print(f"\n{len(themes)} themes, {fails} with contrast failures")
