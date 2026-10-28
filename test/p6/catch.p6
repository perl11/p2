# CATCH { default { } } handles the exceptions of the statements of its block
sub f { die "oops"; CATCH { default { say "caught in f" } } }   #=> caught in f
f();
say "after";                     #=> after
for 1, 2 {
    CATCH { default { say "c"; } }                #=> c
    die "x" if $_ == 1;
    say "v$_";                   #=> v2
}
say "done";                      #=> done
