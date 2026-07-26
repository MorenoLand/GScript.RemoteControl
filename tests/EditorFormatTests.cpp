#include "TEditorFormat.h"

#include <cassert>

int main() {
    RC::RCOptions options;
    options.formatindentwidth = 2;
    options.formatusetabs = false;
    options.formattrimtrailing = true;
    options.removelinecomments = true;
    options.removeblockcomments = true;
    options.preserveclientside = true;
    setEditorFormatOptions(options);
    const std::string source = "//#CLIENTSIDE\nfunction test() {   \n// remove\nif (value) {\necho(\"// keep\"); /* remove */\n}\n}\n";
    const std::string expected = "//#CLIENTSIDE\nfunction test() {\n\n  if (value) {\n    echo(\"// keep\"); \n  }\n}\n";
    assert(formatEditorText(source) == expected);
    options.removelinecomments = false;
    options.removeblockcomments = false;
    setEditorFormatOptions(options);
    assert(formatEditorText(source).find("// remove") != std::string::npos);
    assert(formatEditorText(source).find("/* remove */") != std::string::npos);
    return 0;
}
