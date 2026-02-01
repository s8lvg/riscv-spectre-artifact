"""ANSI color codes for terminal output."""

import sys


class Colors:
    """ANSI color codes for formatted terminal output."""

    RESET = '\033[0m'
    BOLD = '\033[1m'
    DIM = '\033[2m'

    # Colors
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    MAGENTA = '\033[95m'
    CYAN = '\033[96m'
    WHITE = '\033[97m'
    GRAY = '\033[90m'

    @staticmethod
    def disable():
        """Disable colors for non-terminal output."""
        Colors.RESET = ''
        Colors.BOLD = ''
        Colors.DIM = ''
        Colors.RED = ''
        Colors.GREEN = ''
        Colors.YELLOW = ''
        Colors.BLUE = ''
        Colors.MAGENTA = ''
        Colors.CYAN = ''
        Colors.WHITE = ''
        Colors.GRAY = ''


# Auto-disable colors if not a TTY
if not sys.stdout.isatty():
    Colors.disable()
