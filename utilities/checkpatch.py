    r'\.(am|at|etc|in|m4|mk|patch|py)$|debian/rules')
leading_whitespace_blacklist = re.compile(r'\.(mk|am|at)$|debian/rules')
    """Return TRUE if there is not a bracket at the end of an if, for, while
    if len(line) > 79:
        print_warning("Line is %d characters long (recommended limit is 79)"
                      % len(line))