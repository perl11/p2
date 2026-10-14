use utf8; binmode(STDOUT, ":utf8");
my $größe = 5; my $テスト = 7;
sub 텟 { return "k" . shift }
package 텟Ṱ::Пр; sub new { bless {}, shift } sub name { return "obj" }
package main;
print "$größe $テスト ", 텟(3), "|", ref(텟Ṱ::Пр->new), "|", 텟Ṱ::Пр->new->name, "\n"; #=> 5 7 k3|텟Ṱ::Пр|obj
