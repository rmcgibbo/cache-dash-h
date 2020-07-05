{ stdenv, cmake, cmakeCurses
  , sqlite, python, which }:

stdenv.mkDerivation rec {
  name = "cache-dash-h";
  src = ./.;
  buildInputs = [ sqlite ];
  nativeBuildInputs = [ cmake cmakeCurses ];  
  cmakeFlags = ["-DSQLITECPP_INTERNAL_SQLITE=OFF"];
  checkPhase = "ctest";
  doCheck = true;
  checkInputs = [python which];
}
