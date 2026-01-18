#!/bin/bash

# Find the best available C compiler
find_compiler() {
    if command -v clang >/dev/null 2>&1; then
        echo "clang"
    elif command -v gcc >/dev/null 2>&1; then
        echo "gcc"
    elif command -v cc >/dev/null 2>&1; then
        echo "cc"
    else
        echo "Error: No C compiler found" >&2
        exit 1
    fi
}

# Main compilation logic
main() {
    local compiler=$(find_compiler)
    local c_file=""
    local output=""
    local optimize=false
    local debug=false

    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -o|--output)
                output="$2"
                shift 2
                ;;
            -O|--optimize)
                optimize=true
                shift
                ;;
            -g|--debug)
                debug=true
                shift
                ;;
            *.c)
                c_file="$1"
                shift
                ;;
            *)
                echo "Usage: $0 [options] <file.c>"
                echo "Options:"
                echo "  -o, --output <name>    Output executable name"
                echo "  -O, --optimize         Enable optimization (-O3)"
                echo "  -g, --debug           Enable debug symbols (-g)"
                exit 1
                ;;
        esac
    done

    # Check if C file was provided
    if [[ -z "$c_file" ]]; then
        echo "Error: No C file specified" >&2
        echo "Usage: $0 [options] <file.c>" >&2
        exit 1
    fi

    # Check if file exists
    if [[ ! -f "$c_file" ]]; then
        echo "Error: File '$c_file' not found" >&2
        exit 1
    fi

    # Set default output name if not specified
    if [[ -z "$output" ]]; then
        output="${c_file%.c}"
    fi

    # Build compiler flags
    local flags=()
    if [[ "$optimize" == true ]]; then
        flags+=("-O3")
    fi
    if [[ "$debug" == true ]]; then
        flags+=("-g")
    fi

    echo "Using compiler: $compiler"
    echo "Compiling: $c_file -> $output"

    # Compile
    "$compiler" "${flags[@]}" -o "$output" "$c_file"

    if [[ $? -eq 0 ]]; then
        echo "Compilation successful!"
        echo "Run with: ./$output"
    else
        echo "Compilation failed!" >&2
        exit 1
    fi
}

main "$@"
