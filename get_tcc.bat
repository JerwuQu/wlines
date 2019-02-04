@echo off

powershell "Invoke-WebRequest -Uri 'https://ramse.se/random/wlines-tcc-ext.exe' -OutFile wlines-tcc-ext.exe"
wlines-tcc-ext.exe -otcc -y
del wlines-tcc-ext.exe
