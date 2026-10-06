#!perl
# p2-specific regression test for real p5 hashes (%h backed by a
# PNTable, not a Tuple -- see Tuple#table in core/table.c)

my %h = (a => 1, b => 2, c => 3);
say $h{a};
say $h{b};
say $h{c};

$h{d} = 4;
say $h{d};

$h{a} = 99;
say $h{a};

say %h->length;

my %quoted = ("x" => 10, "y" => 20);
say $quoted{"x"};
say $quoted{"y"};

my $v = 5;
my %dyn = (n => $v, m => $v + 1);
say $dyn{n};
say $dyn{m};
