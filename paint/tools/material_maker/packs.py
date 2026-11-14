# Reading the MMPK packs of paint/assets/material_maker
import json, struct, zlib


class Pack:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        _, n, isz = struct.unpack('<III', self.d[4:16])
        self.index = json.loads(self.d[16:16 + isz])
        self.base = 16 + isz

    def raw(self, e):
        return zlib.decompress(self.d[self.base + e['o']: self.base + e['o'] + e['n']], -15) if e['n'] else b''

    def json(self, e):
        return json.loads(self.raw(e))

    def find(self, key, value):
        for e in self.index:
            if e.get(key) == value:
                return e
        return None
