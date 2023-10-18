# Copy files to build directory for testing purposes
vs_files = files(
    'aegisub_vs.py',
)

foreach f: vs_files
    configure_file(input: f, output: '@PLAINNAME@', copy: true)
endforeach
