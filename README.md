<p align="center">
  <img src="Byte.png" width="150" border="2">
</p>
Byte Programming Language (.by)
​Byte is a specialized, high-level scripting language designed for Security Researchers, OSINT Analysts, and Penetration Testers. Built upon the robust and lightning-fast Lua 5.5 engine, Byte bridges the gap between low-level performance and high-level simplicity. It allows you to write complex network and system tools with minimal code.
​⚠️ Platform Availability
​NOTICE: This version of Byte is strictly built and optimized for Kali Linux and Termux (Android).
​Versions for Windows and macOS are currently in development and will be released soon.
​🚀 The Development Journey (Challenges)
​Creating Byte wasn't just about wrapping an engine; it was about solving specific workflow problems for hackers:
​The "Smart I/O" Struggle: One of our biggest challenges was the I/O logic. We wanted a language where functions like net.geoip() could work in two ways: print the result directly to the terminal for quick inspection (Interactive Mode) OR return the value to a variable for further processing (Scripting Mode). Engineering this "Dual-Output" behavior in C while keeping the syntax clean was a major hurdle.
​C-Native Performance: We focused on making the core libraries (net, osb, color) as native as possible. Integrating raw C-system calls with a high-level scripting interface required deep optimization of the Stack-Virtual Machine.
​📦 Language Features
​⚡ Hybrid Execution: Combines the speed of C-based modules with the flexibility of a dynamic script.
​🐍 Pythonic Simplicity: Minimalist syntax that feels familiar and easy to master.
​🛡️ Built for Hacking: Native support for networking, system architecture analysis, and terminal manipulation.
​🎨 ANSI-Integrated: Every string can be colored and formatted natively without external dependencies.
​📚 Standard Libraries (v1.0)
​Byte currently features 3 powerful core libraries:
​net: Specialized for OSINT. Methods: geoip, whois, scan, dns, public_ip, download.
​osb: System Management. Methods: run (execute system commands), cpu, user, list, mkdir, temp.
​color: Theming and Terminal UI. Methods: red, green, cyan, bold, reset.
​💻 Code Examples (Kali/Termux Exclusive)
​1. Advanced OSINT & Port Scanner
​This script demonstrates how Byte handles loops and network modules simultaneously.
