import pathlib, tomllib, json


class Cfg:
    def __init__(self, path):
        self.path = pathlib.Path(path).resolve()
        self.base = self.path.parent.parent          # <root>/ (configs/ is one level below)
        with open(self.path, "rb") as f:
            self.d = tomllib.load(f)

    def get(self, sec, key, default=None):
        return self.d.get(sec, {}).get(key, default)

    def path_of(self, sec, key, default):
        p = pathlib.Path(self.get(sec, key, default))
        return p if p.is_absolute() else self.base / p

    def catalog(self):
        with open(self.path.parent / "catalog.json", encoding="utf-8") as f:
            return json.load(f)
