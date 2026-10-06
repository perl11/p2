#!perl
# p2-specific regression test for object/lobby-level builtins in p5
# mode: closure introspection (arity, code), lobby self/kind/string,
# and the mt19937 rand/srand pair.

# Closure arity
my $sub = sub ($a, $b) { $a + $b };
my $arity = $sub->arity;
say $arity;

# Closure code (string representation of the compiled proto)
my $code_str = $sub->code;
say $code_str;

# Lobby self and kind
my $self = self;
say $self;

my $kind = self->kind;
say $kind;

# Lobby string
my $lobby_str = self->string;
say $lobby_str;

# Lobby rand/srand (mt19937, seeded rand is deterministic)
srand(42);
my $rand1 = rand;
say $rand1;

srand(42);
my $rand2 = rand;
say $rand2;
