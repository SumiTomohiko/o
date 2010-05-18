
%include {
#include <assert.h>
}

cond ::= expr. {
}
expr ::= or_expr. {
}
or_expr ::= and_expr. {
}
or_expr ::= or_expr OR and_expr. {
}
and_expr ::= not_expr. {
}
and_expr ::= and_expr AND not_expr. {
}
not_expr ::= phrases. {
}
not_expr ::= not_expr NOT fuzzy_expr. {
}
phrases ::= fuzzy_expr. {
}
phrases ::= phrases fuzzy_expr. {
}
fuzzy_expr ::= atom. {
}
fuzzy_expr ::= PHRASE QUESTION. {
}
atom ::= PHRASE. {
}
atom ::= LPAR expr RPAR. {
}

/**
 * vim: tabstop=4 shiftwidth=4 expandtab softtabstop=4
 */
