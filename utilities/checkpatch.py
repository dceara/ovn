MAX_LINE_LEN = 79
    r'\.(am|at|etc|in|m4|mk|patch|py|dl)$|debian/rules')
    """Return TRUE if there is a bracket at the end of an if, for, while
        # Early return to avoid potential catastrophic backtracking in the
        # __regex_if_macros regex
        if len(line) == MAX_LINE_LEN - 1 and line[-1] == ')':
            return True

    if len(line) > MAX_LINE_LEN:
        print_warning("Line is %d characters long (recommended limit is %d)"
                      % (len(line), MAX_LINE_LEN))