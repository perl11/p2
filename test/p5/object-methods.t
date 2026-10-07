#!perl
# p2-specific regression test for object/lobby-level builtins in p5
# mode: closure introspection, lobby self/kind/string, and the mt19937
# rand/srand pair (seeded rand is deterministic). The closure 'code'
# dump is not pinned here: its multi-line output carries trailing
# whitespace per line, which does not survive in expected-value comment
# markers. Expected stdout is pinned inline, compared by
# test/runtests.sh -p5.

# Closure arity
my $sub = sub ($a, $b) { $a + $b };
my $arity = $sub->arity;
say $arity; #=> 2

# Lobby self and kind
my $self = self;
say $self; #=> P2

my $kind = self->kind;
say $kind; #=> Mixin

# Lobby string
my $lobby_str = self->string;
say $lobby_str; #=> P2

# Lobby rand/srand (mt19937, seeded rand is deterministic)
srand(42);
my $rand1 = rand;
say $rand1; #=> 1608637542

srand(42);
my $rand2 = rand;
say $rand2; #=> 1608637542
