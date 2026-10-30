# the parenthesized statement-modifier value form (EXPR if COND) /
# (EXPR unless COND): the chosen branch is the value; the other is Nil.
# In list context Nil contributes nothing (my @x = 1, (2 if 0), 3 has 2
# elements); in scalar context Nil is falsey and numifies to 0. Implemented
# as PVIP_NODE_IF_EXPR / PVIP_NODE_UNLESS_EXPR (syn/syntax-p6.y), translated to
# the if/else value shape (syn/pvip_to_pn.c) with a Nil else-branch; Nil is
# dropped from list collection (potion_tuple_push_skipnil, OP_SETTUPLE).
say (42 if True);              #=> 42
say (42 if True) + 1;          #=> 43
say (42 if False) + 1;         #=> 1
my @x = 41, (42 if True), 43;
say @x.elems;                  #=> 3
my @y = 41, (42 if False), 43;
say @y.elems;                  #=> 2
say (7 unless False);          #=> 7
say (7 unless False) + 1;      #=> 8
my @z = (1 unless True), 2, (3 unless False);
say @z.elems;                  #=> 2
my $v = (10 if True) + (20 unless False);
say $v;                        #=> 30
