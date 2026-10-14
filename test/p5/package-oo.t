#!perl
# perl5 packages as classes: bless, method calls along @ISA, SUPER::, can/isa, element
# increments, "$r->{k}" interpolation, my ($c, %args) = @_.
package Animal;
sub new { my ($class, %a) = @_; my $self = { name => $a{name} || "x", legs => 4 }; return bless $self, $class; }
sub speak { my $self = shift; return $self->{name} . " makes a sound"; }
sub name { $_[0]->{name} }
package Dog;
our @ISA = ('Animal');
sub speak { my $self = shift; return $self->SUPER::speak() . " (woof)"; }
package Counter;
my $count = 0;
sub new { my $class = shift; my $self = bless { n => 0, @_ }, $class; $count++; return $self; }
sub inc { my $self = shift; $self->{n}++; return $self; }
sub n { $_[0]{n} }
sub count { $count }
package Sub::Counter;
use parent -norequire, 'Counter';
sub inc { my $self = shift; $self->SUPER::inc(); $self->SUPER::inc(); return $self; }
package main;
my $d = Dog->new(name => "Rex");
say $d->speak; #=> Rex makes a sound (woof)
say ref($d), "|", ($d->isa('Animal') ? "isa" : "no"), "|", (Dog->can('name') ? "can" : "cannot"), "|", (Dog->can('fly') ? "can" : "cannot"); #=> Dog|isa|can|cannot
my $c = Counter->new; $c->inc->inc;
my $s = Sub::Counter->new(n => 10); $s->inc;
say $c->n, "|", $s->n, "|", Counter->count, "|", ref($s), "|", (UNIVERSAL::isa($s, 'HASH') ? "hash" : "nohash"); #=> 2|12|2|Sub::Counter|hash
say __PACKAGE__; #=> main
my $r = { n => 7, l => [1, 2], h => { a => "x" } };
my %h = (a => { b => 5 });
say "v=$r->{n} l=$r->{l}[1] h=$r->{h}{a}| $h{a}{b} $r->{n}th"; #=> v=7 l=2 h=x| 5 7th
$r->{n} += 5; $r->{n}++; ++$r->{l}[0];
my $old = $h{a}{b}++;
say "$r->{n} $r->{l}[0] $old $h{a}{b}"; #=> 13 2 5 6
sub f { my ($c, %a) = @_; return "$c|$a{name}|$a{x}"; }
say f("C", name => "Rex", x => 2); #=> C|Rex|2
my ($first, @rest) = (1, 2, 3);
say "$first|@rest"; #=> 1|2 3
package Ovl; use overload '""' => sub { "Ovl(" . $_[0]{v} . ")" }, fallback => 1;
sub new { bless { v => $_[1] }, $_[0] }
package main;
my $ov = Ovl->new(3);
say "$ov|", ($ov eq "Ovl(3)" ? "eq" : "ne"), "|", $ov . "x", "|", join(",", $ov, $ov); #=> Ovl(3)|eq|Ovl(3)x|Ovl(3),Ovl(3)
my $late; eval { $late = 5 };
say "[$late]"; #=> [5]
sub cnt { return scalar(@_) . ":" . join(",", @_); }
my @ca = (1, 2);
say cnt((1 == 1), 5), "|", cnt(@ca), "|", cnt(0, @ca, 9), "|", cnt((@ca), 3), "|", cnt(()), "|", cnt((7, 8), 9); #=> 2:1,5|2:1,2|4:0,1,2,9|3:1,2,3|0:|3:7,8,9
