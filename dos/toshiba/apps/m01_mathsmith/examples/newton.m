function r = newton(a)
% the square root of a by Newton's method
r = a;
for k = 1:20
  r = (r + a / r) / 2;
end
end
