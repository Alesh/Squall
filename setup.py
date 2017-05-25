from setuptools import setup

setup(**{
    'name': 'squall',
    'version': '0.1.dev20170524',
    'author': 'Alexey Poryadin',
    'author_email': 'alexey.poryadin@gmail.com',
    'description': "Includes files for using the Squall template library"
                   "to build a Python C/C++ extensions.",
    'headers': ['include/squall/Dispatcher.hxx',
                'include/squall/NonCopyable.hxx',
                'include/squall/PlatformLoop.hxx',
                'include/squall/PlatformWatchers.hxx'],
    'zip_safe': False
})
