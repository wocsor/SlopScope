"""Read the separate viewer from a checkout or its published static site."""
from pathlib import Path
from urllib.error import URLError
from urllib.parse import urljoin, urlparse
from urllib.request import urlopen

DEFAULT_VIEWER_URL = "https://wocsor.github.io/slopscope-web/"


class ViewerAssets:
    def __init__(self, directory=None, url=None):
        self.directory = Path(directory).expanduser().resolve() if directory else None
        if self.directory is None and url is None:
            sibling = Path(__file__).resolve().parents[4] / "slopscope-web/src/slopscope_web/static"
            if (sibling / "index.html").is_file():
                self.directory = sibling
        if self.directory is not None and not (self.directory / "index.html").is_file():
            raise ValueError(f"Viewer index.html is missing from {self.directory}")
        self.url = (url or DEFAULT_VIEWER_URL).rstrip("/") + "/"
        if urlparse(self.url).scheme not in ("https", "http"):
            raise ValueError("The viewer URL must use HTTP or HTTPS")
        self.cache = {}

    def read(self, filename):
        if Path(filename).name != filename or not filename.endswith((".html", ".js", ".css")):
            raise ValueError("Invalid viewer asset name")
        if self.directory is not None:
            return (self.directory / filename).read_bytes()
        if filename not in self.cache:
            try:
                with urlopen(urljoin(self.url, filename), timeout=10) as response:
                    data = response.read(2 * 1024 * 1024 + 1)
                if len(data) > 2 * 1024 * 1024:
                    raise ValueError("Viewer asset exceeds 2 MiB")
                self.cache[filename] = data
            except (URLError, OSError) as error:
                raise RuntimeError(
                    f"Could not load the viewer from {self.url}. "
                    "Use --ui-dir /path/to/slopscope-web/src/slopscope_web/static "
                    "or --viewer-url URL."
                ) from error
        return self.cache[filename]
