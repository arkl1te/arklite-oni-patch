"""Refuses maps that pack a texture of the game.

    python3 -I tools/mapcheck.py <GameDataFolder> <map folder>...

A Catwalk map names the game's textures and Arklite loads them from the player's own
levels, so a TXMP or TXAN with data under a name the game's levels also have is a texture
of the game packed by mistake (an old build, or a map from before Catwalk did this).
"""
import glob, os, struct, sys

def instances(path):
    """(tag, name or None, has data) of each instance in an Oni .dat file."""
    b = open(path, 'rb').read()
    count, = struct.unpack_from('<I', b, 0x14)
    names_at, = struct.unpack_from('<I', b, 0x28)
    for i in range(count):
        tag, _, name_off, size, flags = struct.unpack_from('<4sIIII', b, 0x40 + i * 20)
        name = None
        if not flags & 1:                       # 1: unnamed (private)
            s = names_at + name_off
            name = b[s:b.index(b'\0', s)].decode('latin1')
        yield tag[::-1].decode('latin1'), name, size > 0 and not flags & 2   # 2: placeholder

def main():
    game, maps = sys.argv[1], sys.argv[2:]
    stock = set()
    for f in glob.glob(os.path.join(game, 'level*_Final.dat')):
        stock.update(n for t, n, _ in instances(f) if n and t in ('TXMP', 'TXAN'))
    if not stock:
        sys.exit(f'no textures found in {game}/level*_Final.dat')
    bad = 0
    for m in maps:
        packed = sorted(n for t, n, data in instances(os.path.join(m, 'level50_Final.dat'))
                        if data and n in stock)
        if packed:
            bad += 1
            print(f'{os.path.basename(m)}: packs {len(packed)} texture(s) of the game, rebuild it in Catwalk: '
                  + ', '.join(packed[:5]) + (' ...' if len(packed) > 5 else ''))
    if bad:
        sys.exit(1)
    print(f'checked {len(maps)} map(s): no textures of the game')

main()
