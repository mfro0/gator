Translation by Ronald Delobel

Si vous utilisez un noyau de la serie 2.4 assurez vous d'appliquer les
patchs video4linux disponibles sur http://bytesex.org/patches/ 
Pour compiler taper make
 
Si vous voulez compiler pour une version diff�rente du noyau vous pouvez
utiliser make KDIR=/usr/src/linux-2.x.xx 
Pour charger le module avec un noyau 2.4.x tapez
insmod ./genericv4l.o disableinterlace=1
Pour charger le module avec un noyau 2.6.x tapez
insmod ./genericv4l.ko disableinterlace=1
(l'option disableinterlace limite la r�solution de capture � 720x240 NTSC
mais la qualit� est meilleure qu'en 720x480 avec entrelacement :) 
Puis lancez xawtv ou n'importe quel autre programme utilisant v4l
 
Pour obtenir une liste des options disponibles pour le module tapez
modinfo genericv4l.o  (noyau 2.4.x)
ou
modinfo genericv4l.ko  (noyau 2.6.x)
 
SI LE MODULE NE SE CHARGE PAS et que vous utilisez un noyau 2.4
La premi�re chose � faire est de vous assurer que vous avez appliqu� les
patchs v4l2 correspondants � votre version du noyau. 
Par exemple pour un noyau 2.4.25 proc�dez ainsi
cd /usr/src
wget http://bytesex.org/patches/2.4.25-rc1-1/20_v4l2-2.4.25-rc1.diff.gz
gunzip 20_v4l2-2.4.25-rc1.diff.gz
cd /usr/src/linux
#utilisez l'option --dry-run pour s'assurer que le patch s'appliquera
#correctement
patch --dry-run -p1 <../20_v4l2-2.4.25-rc1.diff
#si aucune erreur n'est report�e appliquez le patch d�finitivement
patch -p1 <../20_v4l2-2.4.25-rc1.diff
 
#puis lancer la compilation
make; make modules; make modules_install; make install
(ou make bzImage ou votre cible pr�f�r�e :)
 
Si vous utilisez un noyau de la serie 2.6 vous n'avez pas besoin
d'appliquer le moindre patch. 
Si pour une raison ou une autre �a ne marchait pas envoyez un mail (en
anglais svp)! 
Merci d'inclure la sortie de dmesg et du programme avec votre courrier. 

NOTE: si au chargement du module le tuner n'est pas trouv� (regardez la
sortie de dmesg) vous pouver essayer de sp�cifier le type de tuner avec
le param�tre tunertype.
Si vous poss�dez un tuner SECAM vous DEVEZ sp�cifier tunertype=2
