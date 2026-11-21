"""SlopScope USB control, waveform decoding and calibration."""
from .device import SlopScope, decode_frame, fields
from .discovery import ScopeNotConnected, ScopeSelectionRequired, list_scopes

__version__ = "0.1.0"
__all__ = ["SlopScope", "decode_frame", "fields", "list_scopes", "ScopeSelectionRequired", "ScopeNotConnected"]
