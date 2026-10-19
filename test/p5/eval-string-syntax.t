#!perl
# single quoted strings: \\ and \' are escapes, other backslashes are literal;
# "${name}" interpolates; a syntax error in eval "string" sets $@ and the program goes on
my $one = 'a';
say length('\\\\'), "|", 'it\'s', "|", 'a\nb';   #=> 2|it's|a\nb
say "${one}b|\\${one}";                          #=> ab|\a
my $r = eval "1 + ) 2";
say defined $r ? "def" : "undef", "|", ($@ ? "err" : "noerr");   #=> undef|err
say eval "2 + 3", "|", ($@ eq "" ? "clean" : "dirty");           #=> 5|clean
