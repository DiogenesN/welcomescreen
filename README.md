# WelcomeScreen
A small command line utility for Wayland that shows a short welcome screen.
It was tested on Debian 12 on [woodland](https://github.com/DiogenesN/woodland).

# Before building
   install the following libs:

		make
		pkgconf
		libcairo2-dev
		libwayland-dev

   on Debian run the following command:

		sudo apt install make libcairo2-dev libwayland-dev

# Installation/Usage
  1. Open a terminal and run:

		 chmod +x ./configure
		 ./configure

  2. if all went well then run:

		 make
		 sudo make install
		 
		 (if you just want to test it then run: make run)

  3. run:
  
		 welcomescreen --resolution 1920x1080

That's it!

# Support

   My Libera IRC support channel: #linuxfriends

   Matrix: https://matrix.to/#/#linuxfriends2:matrix.org

   Email: nicolas.dio@protonmail.com
