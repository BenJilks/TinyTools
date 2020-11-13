#include <stdio.h>
#include <stdlib.h>
#include "lexer.h"
#include "parser.h"
#include "preproccessor.h"
#include "dumpast.h"
#include "x86.h"
#include <ctype.h>

static void debug_dump_no_empty_lines(Stream *stream)
{
    int is_start_of_line = 1;
    for (int i = 0; i < stream->memory_length; i++)
    {
        char c = stream->memory[i];
        if (is_start_of_line)
        {
            if (isspace(c))
                continue;
            is_start_of_line = 0;
        }

        putc(c, stdout);
        if (c == '\n')
            is_start_of_line = 1;
    }
}

int main()
{
    Stream input_stream = stream_create_input_file("test.c");
    Stream output_stream = stream_create_output_memory();
    pre_proccess_file(&input_stream, &output_stream);

    //fwrite(output_stream.memory, 1, output_stream.memory_length, stdout);
    //debug_dump_no_empty_lines(&output_stream);

    stream_close(&input_stream);
    lexer_open_memory(output_stream.memory, output_stream.memory_length);

    // Parse
    Unit *unit = parse();
    dump_unit(unit);

    // Compile
    X86Code code = x86_compile_unit(unit);
    x86_dump(&code);

    // Clean up
    free_x86_code(&code);
    free_unit(unit);
    free(unit);

    lexer_close();
    return 0;
}
