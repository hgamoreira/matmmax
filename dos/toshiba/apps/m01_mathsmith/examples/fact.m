function f = fact(n)
% n factorial, by recursion
if n <= 1
  f = 1;
else
  f = n * fact(n - 1);
end
end
