/*
 * no_init_array.ld — supprime .init_array / .fini_array du sysmodule.
 *
 * POURQUOI (crash hardware confirmé, crash_dump_00000010) :
 *   Le CXI contient bien .init_array[0] = &frame_dummy (0x00100dc0), mais ces
 *   tableaux tombent dans la DERNIÈRE page partielle du segment RW, juste à la
 *   frontière du .bss. Le loader NCCH (Luma3DS) zéro-remplit cette zone au
 *   chargement -> à l'exécution .init_array[0] vaut 0. __libc_init_array()
 *   fait alors `blx r3` avec r3=0 -> saut à l'adresse 0 (pc=0, prefetch abort),
 *   AVANT main().
 *
 * Ce sysmodule est en C pur : la seule entrée de .init_array est frame_dummy
 * (enregistrement des frames d'exception C++ via __register_frame_info), qui
 * n'est jamais utilisé ici (aucune exception C++, mbedtls/wslay/jansson sont en
 * C). On peut donc supprimer .init_array/.fini_array sans effet fonctionnel :
 * __libc_init_array boucle alors 0 fois (__init_array_start == __init_array_end)
 * et ne déréférence plus de pointeur nul.
 *
 * Utilisé via `-Wl,-T,no_init_array.ld` : le mot-clé INSERT complète le script
 * de lien par défaut (fourni par -specs=3dsx.specs) au lieu de le remplacer.
 * Placé APRÈS .data, ce /DISCARD/ capte les sections d'entrée .init_array/
 * .fini_array avant l'output section .init_array du script par défaut.
 */
SECTIONS
{
  /DISCARD/ :
  {
    *(.init_array)
    *(.init_array.*)
    *(.fini_array)
    *(.fini_array.*)
  }
}
INSERT AFTER .data;
