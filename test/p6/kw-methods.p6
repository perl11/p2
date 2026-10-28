# methods named like loop keywords are method calls, last/next stay loop control
class C {
    method break($x) { say "break $x" }   #=> break 1
    method next { say "next" }            #=> next
    method last { say "last" }            #=> last
}
my $c = C.new;
$c.break(1);
$c.next;
$c.last;
for 1, 2, 3 { next if $_ == 2; say $_ }   #=> 1
                                          #=> 3
