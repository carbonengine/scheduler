import sys
import unittest

sys.modules['scheduler'] = sys

def print_suite(suite):
    if hasattr(suite, '_exception'):
        print(suite._exception)
        sys.exit(1)
    else:
        if not hasattr(suite, '__iter__'):
            print(suite.id())
        else:
            [print_suite(suite) for suite in suite]


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.discover('tests')
    print_suite(suite)
